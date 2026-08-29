#include "cdp/WebDriver.h"

#include "cdp/CdpClient.h"
#include "utils/Utils.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QThread>

WebDriver::WebDriver(QObject *parent)
    : QObject(parent)
{
}

void WebDriver::init(CdpClient *cdp)
{
    m_cdp = cdp;
}

QString WebDriver::jsStr(const QString &text)
{
    // QJsonDocument::fromVariant(QString) trong Qt6 trả document RỖNG -> không
    // dùng được cho chuỗi đơn. Bọc qua QJsonArray rồi cắt phần giá trị (giữ cả
    // dấu nháy) để lấy chuỗi đã escape chuẩn JSON (hợp lệ trong JavaScript).
    const QByteArray json =
        QJsonDocument(QJsonArray{QJsonValue(text)}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

bool WebDriver::enableDomains()
{
    if (!m_cdp)
        return false;
    // Enable không cần chờ phản hồi: CDP thực thi theo thứ tự trên cùng kết nối,
    // nên các lệnh này luôn xong trước các lệnh dùng domain (Page/Runtime) phía sau.
    // KHÔNG bật Network domain: không có code nào đọc sự kiện Network.* nhưng
    // Chrome sẽ bắn hàng chục nghìn event Network.requestWillBeSent/responseReceived
    // mỗi giây cho MỌI tài nguyên của trang Facebook — parse JSON từng event này
    // (trong CdpClient::onTextMessageReceived) từng đốt CPU lớn khi mở nhiều tab.
    m_cdp->sendCommand(QStringLiteral("Runtime.enable"), {}, 0, false);
    m_cdp->sendCommand(QStringLiteral("Page.enable"), {}, 0, false);
    // Theo dõi sẵn sự kiện load ngay khi bật domain: domContentEventFired có thể
    // nổ trong lúc lệnh khác đang chờ phản hồi (chưa ai gọi waitForEvent) —
    // nếu chưa được theo dõi, event sẽ bị bộ lọc chuỗi thô drop và lần chờ
    // kế tiếp phải chịu nguyên timeout. Đây là method duy nhất app cần.
    m_cdp->watchEvent(QStringLiteral("Page.domContentEventFired"));
    return true;
}

void WebDriver::initPageHooks()
{
    if (!m_cdp)
        return;
    // Đăng ký hook qua Page.addScriptToEvaluateOnNewDocument để __apxpath tự
    // xuất hiện trong MỌI document sau navigate (trước đây dùng evaluate tạm thời
    // nên bị mất mỗi lần tải trang mới -> các check XPath lặng lẽ thất bại).
    const QString script = QStringLiteral(
        "window.__apxpath=function(expr){"
        "var r=document.evaluate(expr,document,null,XPathResult.ORDERED_NODE_SNAPSHOT_TYPE,null);"
        "var out=[];for(var i=0;i<r.snapshotLength;i++)out.push(r.snapshotItem(i));"
        "return out;};true");
    QJsonObject params{{QStringLiteral("source"), script}};
    m_cdp->sendCommand(QStringLiteral("Page.addScriptToEvaluateOnNewDocument"), params);
    evaluate(script);
}

void WebDriver::addAntiDetectionScript(const QString &ua, const QString &platform,
                                        int screenWidth, int screenHeight,
                                        const QStringList &languages,
                                        const QString &webglVendor, const QString &webglRenderer,
                                        int hardwareConcurrency, int deviceMemory,
                                        quint64 personaSeed)
{
    if (!m_cdp)
        return;

    // Các giá trị nhúng vào script: mọi chuỗi được escape an toàn via jsStr.
    const QString uaJson = jsStr(ua);
    const QString platformJson = jsStr(platform);
    const QString vendorJson = jsStr(webglVendor);
    const QString rendererJson = jsStr(webglRenderer);
    // Danh sách ngôn ngữ phải khớp với UA/OS của persona (không hardcode vi-VN).
    // Xuất thành mảng JS literal hợp lệ qua QJsonDocument (không bọc thành chuỗi).
    QJsonArray langArr;
    for (const QString &l : languages)
        langArr.append(l);
    const QString langsJson =
        QString::fromUtf8(QJsonDocument(langArr).toJson(QJsonDocument::Compact));

    const QString script = QStringLiteral(
        "(() => {"
        "  const ua = %1, platform = %2, langs = %9;"
        // webdriver=false thay vì undefined: Chrome 120+ cho phép đọc phẳng,
        // get:() => undefined cũng bị một số detector bắt lỗi (so sánh === false).
        "  Object.defineProperty(navigator, 'webdriver', {get: () => false});"
        // Ngụy trang toString: mọi hàm/property ta override đều phải trả về chuỗi
        // "[native code]" như hàm thật — toString() lộ mã nguồn override là điểm
        // machine hàng đầu mà detector probe.
        "  const nativeStr = (n) => 'function ' + n + '() { [native code] }';"
        "  const nuku = (o, p, n) => { try {"
        "    const ts = Object.getOwnPropertyDescriptor(o, p);"
        "    if (ts && ts.get) {"
        "      Object.defineProperty(ts.get, 'toString', {value: () => nativeStr(n), writable:true, configurable:true});"
        "    } else if (ts && typeof o[p] === 'function') {"
        "      Object.defineProperty(o[p], 'toString', {value: () => nativeStr(n), writable:true, configurable:true});"
        "    }"
        "  } catch (e) {} };"
        "  if (!window.chrome) {"
        "    window.chrome = {runtime:{}, loadTimes:function(){}, csi:function(){},"
        "      app:{isInstalled:false}, webstore:{}};"
        "  }"
        "  const mkPlugin=(n,f,d)=>({name:n,filename:f,description:d,length:0});"
        "  Object.defineProperty(navigator, 'plugins', {get: () => ["
        "    mkPlugin('PDF Viewer','internal-pdf-viewer','Portable Document Format'),"
        "    mkPlugin('Chrome PDF Viewer','mhjfbmdgcfjbbpaeojofohoefgiehjai',''),"
        "    mkPlugin('Chromium PDF Viewer','jnbldpbgfcfaihgdgcmcdmgmghjfkkbj','')"
        "  ]});"
        "  Object.defineProperty(navigator, 'languages', {get: () => langs});"
        // Vân tay phần cứng nhất quán với UA/OS của persona (từ persona, không random).
        "  Object.defineProperty(navigator, 'hardwareConcurrency', {get: () => %3});"
        "  Object.defineProperty(navigator, 'deviceMemory', {get: () => %4});"
        "  try {"
        "    Object.defineProperty(navigator, 'platform', {get: () => platform});"
        "    const np = Navigator.prototype;"
        "    if (np) {"
        "      Object.defineProperty(np, 'userAgent', {get: () => ua});"
        "      Object.defineProperty(np, 'appVersion', {get: () => ua.replace(/^Mozilla\\//, '')});"
        "      Object.defineProperty(np, 'vendor', {get: () => 'Google Inc.'});"
        "    }"
        "  } catch (e) {}"
        // WebGL: thay nhà cung cấp / GPU thật bằng card của persona (cố định
        // theo tài khoản -> 2 tab cùng acc có cùng WebGL, 2 acc khác nhau WebGL).
        "  const patchGL = (proto) => {"
        "    if (!proto) return;"
        "    const orig = proto.getParameter;"
        "    proto.getParameter = function(p) {"
        "      if (p === 37445) return %5;"
        "      if (p === 37446) return %6;"
        "      return orig.call(this, p);"
        "    };"
        "    try { Object.defineProperty(proto.getParameter, 'toString', {value: () => nativeStr('getParameter')}); } catch(e){}"
        "  };"
        "  try {"
        "    patchGL(WebGLRenderingContext.prototype);"
        "    patchGL(WebGL2RenderingContext.prototype);"
        "  } catch (e) {}"
        // Khóa kích thước màn hình khớp với window thật (áp cho mọi document/tab).
        "  try {"
        "    const w = %7, h = %8;"
        "    const g = (v) => ({configurable: true, get: () => v});"
        "    Object.defineProperty(screen, 'width', g(w));"
        "    Object.defineProperty(screen, 'availWidth', g(w));"
        "    Object.defineProperty(screen, 'height', g(h));"
        "    Object.defineProperty(screen, 'availHeight', g(h));"
        "  } catch (e) {}"
        // Chặn WebRTC lộ IP thật qua proxy: stub RTCPeerConnection không bao giờ
        // thu thập ICE candidate -> Facebook không đọc được IP máy vật lý.
        "  try {"
        "    const StubPC = function(){"
        "      this.addEventListener=function(){};this.removeEventListener=function(){};"
        "      this.addIceCandidate=function(){return Promise.resolve();};"
        "      this.setRemoteDescription=function(){return Promise.resolve();};"
        "      this.setLocalDescription=function(){return Promise.resolve();};"
        "      this.createOffer=function(){return Promise.reject(new DOMException('blocked','NotAllowedError'));};"
        "      this.createAnswer=function(){return Promise.reject(new DOMException('blocked','NotAllowedError'));};"
        "      this.getStats=function(){return Promise.resolve({});};"
        "      this.close=function(){};"
        "    };"
        "    Object.defineProperty(window,'RTCPeerConnection',{configurable:true,get:()=>StubPC});"
        "    Object.defineProperty(window,'webkitRTCPeerConnection',{configurable:true,get:()=>StubPC});"
        "    if (navigator.mediaDevices) {"
        "      Object.defineProperty(navigator.mediaDevices,'getUserMedia',"
        "        {configurable:true,get:()=>function(){return Promise.reject(new DOMException('blocked','NotAllowedError'));}});"
        "    }"
        "  } catch (e) {}"
        // Nhiễu vân tay Canvas + Audio. Seed CỐ ĐỊNH từ persona (%10) thay vì
        // Math.random() mỗi lần nạp trang: cùng tài khoản luôn cho CÙNG vân tay
        // canvas/audio giữa mọi phiên — nhất quán giữa các lần chạy, không bị
        // phát hiện do vân tay "đổi liên tục" (tín hiệu automation rõ rệt).
        "  try {"
        "    var seed = %10;"
        "    var rnd = function(i){ var x = Math.sin(seed*13.37 + i)*10000; return x - Math.floor(x); };"
        "    var origTD = HTMLCanvasElement.prototype.toDataURL;"
        "    HTMLCanvasElement.prototype.toDataURL = function(){"
        "      var ctx = this.getContext && this.getContext('2d');"
        "      if (ctx && this.width > 16 && this.height > 16) {"
        "        ctx.fillStyle = 'rgba(0,0,0,0.02)';"
        "        for (var i = 0; i < 3; i++)"
        "          ctx.fillRect(Math.floor(rnd(i)*this.width), Math.floor(rnd(i+3)*this.height), 1, 1);"
        "      }"
        "      return origTD.apply(this, arguments);"
        "    };"
        "    try { Object.defineProperty(HTMLCanvasElement.prototype.toDataURL, 'toString', {value: () => nativeStr('toDataURL')}); } catch(e){}"
        "  } catch (e) {}"
        "  try {"
        "    var aOff = ((%10) % 100000) * 0.00001;"
        "    if (typeof AnalyserNode !== 'undefined') {"
        "      var oGF = AnalyserNode.prototype.getFloatFrequencyData;"
        "      if (oGF) AnalyserNode.prototype.getFloatFrequencyData = function(a){"
        "        oGF.call(this, a);"
        "        for (var i = 0; i < a.length; i++) a[i] += aOff;"
        "      };"
        "    }"
        "  } catch (e) {}"
        "  const q = navigator.permissions && navigator.permissions.query;"
        "  if (q) {"
        "    navigator.permissions.query = (p) => {"
        "      if (p && p.name === 'notifications')"
        "        return Promise.resolve({state: 'denied'});"
        "      return q.call(navigator.permissions, p);"
        "    };"
        "  }"
        // Ngụy trang toString cho các property quan trọng nhất đã override.
        "  try {"
        "    var np2 = Navigator.prototype;"
        "    nuku(np2, 'userAgent', 'get userAgent');"
        "    nuku(np2, 'platform', 'get platform');"
        "    nuku(np2, 'hardwareConcurrency', 'get hardwareConcurrency');"
        "    nuku(np2, 'deviceMemory', 'get deviceMemory');"
        "    nuku(np2, 'languages', 'get languages');"
        "    nuku(navigator, 'webdriver', 'get webdriver');"
        "  } catch (e) {}"
        "})();")
        .arg(uaJson, platformJson)
        .arg(hardwareConcurrency)
        .arg(deviceMemory)
        .arg(vendorJson, rendererJson)
        .arg(screenWidth)
        .arg(screenHeight)
        .arg(langsJson)
        .arg(personaSeed);

    QJsonObject params{{QStringLiteral("source"), script}};
    m_cdp->sendCommand(QStringLiteral("Page.addScriptToEvaluateOnNewDocument"), params);
    evaluate(script);
}

void WebDriver::setUserAgentOverride(const QString &ua, const QString &lang, bool waitForResponse)
{
    if (!m_cdp)
        return;

    // Trích Chrome/x.y.z.w từ UA để dựng Client Hints (navigator.userAgentData) khớp.
    static const QRegularExpression versionRe(QStringLiteral("Chrome/(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)"));
    const QRegularExpressionMatch vm = versionRe.match(ua);
    const QString major = vm.hasMatch() ? vm.captured(1) : QStringLiteral("126");
    const QString full = vm.hasMatch()
                             ? vm.captured(1) + QStringLiteral(".") + vm.captured(2) + QStringLiteral(".") +
                                   vm.captured(3) + QStringLiteral(".") + vm.captured(4)
                             : QStringLiteral("126.0.0.0");
    const QString platform = Utils::platformForUserAgent(ua);

    QJsonObject meta;
    meta.insert(QStringLiteral("brands"),
                QJsonArray{
                    QJsonObject{{QStringLiteral("brand"), QStringLiteral("Not/A)Brand")},
                                {QStringLiteral("version"), QStringLiteral("8")}},
                    QJsonObject{{QStringLiteral("brand"), QStringLiteral("Chromium")},
                                {QStringLiteral("version"), major}},
                    QJsonObject{{QStringLiteral("brand"), QStringLiteral("Google Chrome")},
                                {QStringLiteral("version"), major}}});
    meta.insert(QStringLiteral("fullVersionList"),
                QJsonArray{
                    QJsonObject{{QStringLiteral("brand"), QStringLiteral("Not/A)Brand")},
                                {QStringLiteral("version"), QStringLiteral("8.0.0.0")}},
                    QJsonObject{{QStringLiteral("brand"), QStringLiteral("Chromium")},
                                {QStringLiteral("version"), full}},
                    QJsonObject{{QStringLiteral("brand"), QStringLiteral("Google Chrome")},
                                {QStringLiteral("version"), full}}});
    meta.insert(QStringLiteral("mobile"), false);
    meta.insert(QStringLiteral("architecture"), QStringLiteral("x86"));
    meta.insert(QStringLiteral("bitness"), QStringLiteral("64"));
    meta.insert(QStringLiteral("model"), QString());
    if (platform == QStringLiteral("Win32")) {
        meta.insert(QStringLiteral("platform"), QStringLiteral("Windows"));
        meta.insert(QStringLiteral("platformVersion"), QStringLiteral("13.0.0.0"));
    } else {
        meta.insert(QStringLiteral("platform"), QStringLiteral("Linux"));
        meta.insert(QStringLiteral("platformVersion"), QStringLiteral("6.8.0"));
    }

    QJsonObject params;
    params.insert(QStringLiteral("userAgent"), ua);
    params.insert(QStringLiteral("acceptLanguage"), lang);
    params.insert(QStringLiteral("platform"), platform);
    params.insert(QStringLiteral("userAgentMetadata"), meta);
    m_cdp->sendCommand(QStringLiteral("Network.setUserAgentOverride"), params, 30000,
                       waitForResponse);
}

void WebDriver::setScreenMetrics(int width, int height, bool waitForResponse)
{
    if (!m_cdp)
        return;
    QJsonObject params;
    params.insert(QStringLiteral("width"), width);
    params.insert(QStringLiteral("height"), height);
    params.insert(QStringLiteral("deviceScaleFactor"), 1);
    params.insert(QStringLiteral("mobile"), false);
    params.insert(QStringLiteral("screenWidth"), width);
    params.insert(QStringLiteral("screenHeight"), height);
    m_cdp->sendCommand(QStringLiteral("Emulation.setDeviceMetricsOverride"), params, 30000,
                       waitForResponse);
}

void WebDriver::setTimezone(const QString &timezoneId, bool waitForResponse)
{
    if (!m_cdp)
        return;
    m_cdp->sendCommand(QStringLiteral("Emulation.setTimezoneOverride"),
                       QJsonObject{{QStringLiteral("timezoneId"), timezoneId}}, 30000,
                       waitForResponse);
}

void WebDriver::setLocale(const QString &locale, bool waitForResponse)
{
    if (!m_cdp)
        return;
    m_cdp->sendCommand(QStringLiteral("Emulation.setLocaleOverride"),
                       QJsonObject{{QStringLiteral("locale"), locale}}, 30000, waitForResponse);
}

QJsonValue WebDriver::evaluate(const QString &js, int timeoutMs)
{
    if (!m_cdp)
        return QJsonValue(QJsonValue::Undefined);

    QJsonObject params;
    params.insert(QStringLiteral("expression"), js);
    params.insert(QStringLiteral("returnByValue"), true);
    params.insert(QStringLiteral("awaitPromise"), true);

    const QJsonObject res = m_cdp->sendCommand(QStringLiteral("Runtime.evaluate"), params, timeoutMs);
    if (res.isEmpty() || res.contains(QStringLiteral("error")))
        return QJsonValue(QJsonValue::Undefined);

    const QJsonObject remote = res.value(QStringLiteral("result")).toObject();
    if (remote.contains(QStringLiteral("exceptionDetails")))
        return QJsonValue(QJsonValue::Undefined);
    if (remote.value(QStringLiteral("result")).toObject().contains(QStringLiteral("value")))
        return remote.value(QStringLiteral("result")).toObject().value(QStringLiteral("value"));
    return QJsonValue(QJsonValue::Undefined);
}

bool WebDriver::navigate(const QString &url, int timeoutMs)
{
    if (!m_cdp)
        return false;
    // Xóa dấu vết sự kiện load cũ để chỉ chờ sự kiện của lần navigate này.
    m_cdp->clearRecentEvent(QStringLiteral("Page.domContentEventFired"));
    QJsonObject params{{QStringLiteral("url"), url}};
    m_cdp->sendCommand(QStringLiteral("Page.navigate"), params, 15000);
    return waitForReady(timeoutMs);
}

bool WebDriver::waitForReady(int timeoutMs)
{
    if (!m_cdp)
        return false;
    // Trang Facebook nạp rất nhiều XHR/pixel tracking nên Page.loadEventFired
    // thường trễ 2-10s so với DOMContentLoaded. DOM sẵn sàng (readyState
    // !='loading') là đủ để thao tác DOM -> ưu tiên chờ domContentEventFired,
    // giảm rõ rệt thời gian "chờ trang" mỗi lần điều hướng.
    // KHÔNG clearRecentEvent ở đây: navigate() đã clear TRƯỚC KHI gửi lệnh
    // navigate, nên mọi Page.domContentEventFired trong buffer đều thuộc lần
    // điều hướng hiện tại. Clear lần nữa ngay trước khi chờ sẽ xóa dấu vết
    // sự kiện vừa nổ trong khi lệnh navigate chưa trả về — JoinEngine /
    // NurtureEngine gọi waitForReady sau navigate sẽ bị đốt nguyên timeout
    // (10-30s mỗi lần) chỉ để rồi fallback ở bước cuối.
    if (m_cdp->waitForEvent(QStringLiteral("Page.domContentEventFired"), 0))
        return true;
    if (m_cdp->waitForEvent(QStringLiteral("Page.domContentEventFired"), timeoutMs))
        return true;
    // Dự phòng: trang cũ/beforefire bị bỏ lỡ nhưng DOM thực tế đã sẵn sàng.
    return evaluate(QStringLiteral("document.readyState!=='loading'"), 2000).toBool();
}

bool WebDriver::waitForCondition(const QString &jsCondition, int timeoutMs)
{
    if (!m_cdp)
        return false;
    // Chờ sự kiện phía page thay vì poll CDP mỗi 100ms: điều kiện được kiểm tra
    // NGAY khi DOM thay đổi (MutationObserver, 0ms trễ sau khi thỏa mãn), thay vì
    // từng vòng evaluate + sleep (mỗi vòng 1 round-trip CDP). Tổng cộng: 1 round-trip
    // cho cả lần chờ. Điều kiện: biểu thức JS trả true/false (không làm thay đổi DOM).
    const QString js = QStringLiteral(
        "(function(){"
        "var cond=function(){try{return !!(%1);}catch(e){return false;}};"
        "if(cond())return true;"
        "return new Promise(function(resolve){"
        "var resolved=false,obs=null,beat=null,still=null;"
        "var finish=function(){"
        "if(resolved)return;resolved=true;"
        "try{if(obs)obs.disconnect();}catch(e){}"
        "if(beat)clearInterval(beat);if(still)clearInterval(still);"
        "try{if(marker&&marker.parentNode)marker.parentNode.removeChild(marker);}catch(e){}"
        "resolve(cond());};"
        "var lastSeen=Date.now();"
        "var marker=document.createElement('div');"
        "marker.setAttribute('data-apwait','1');"
        "marker.style.cssText='position:absolute;left:-99999px;width:1px;height:1px;';"
        "var parent=document.body||document.documentElement;parent.appendChild(marker);"
        "obs=new MutationObserver(function(ms){"
        "if(resolved)return;var isBeat=true;"
        "for(var i=0;i<ms.length;i++){"
        "var t=ms[i].target;var tn=(t&&t.tagName)||'';"
        "if(!(tn==='DIV'&&t.getAttribute&&t.getAttribute('data-apwait')==='1')){isBeat=false;break;}}"
        "if(isBeat)return;lastSeen=Date.now();if(cond())finish();});"
        "obs.observe(parent,{childList:true,subtree:true,attributes:true,characterData:true});"
        // Heartbeat: nếu trang tự thêm node cũng được nhịp observer thấy; ngoài ra
        // dùng để phát hiện "trang đã tĩnh" và buộc kiểm tra lại định kỳ (fallback).
        "beat=setInterval(function(){if(resolved)return;"
        "marker.setAttribute('data-apb',''+(Math.random()));},100);"
        "still=setInterval(function(){if(resolved)return;"
        "if(Date.now()-lastSeen>1500){lastSeen=Date.now();if(cond())finish();}},400);"
        "setTimeout(finish,%2);});})()")
                           .arg(jsCondition)
                           .arg(qMax(timeoutMs, 500));
    const QJsonValue v = evaluate(js, qMax(timeoutMs + 6000, 8000));
    return v.toBool();
}

QString WebDriver::currentUrl()
{
    return evaluate(QStringLiteral("location.href")).toString();
}

QString WebDriver::pageSource()
{
    return evaluate(QStringLiteral("document.documentElement.outerHTML"), 20000).toString();
}

void WebDriver::scrollToBottom()
{
    evaluate(QStringLiteral("window.scrollTo(0, document.body.scrollHeight); true"));
}

void WebDriver::randomScroll()
{
    // Cuộn kiểu người: nhiều bước nhỏ với dừng ngẫu nhiên, thỉnh thoảng cuộn
    // ngược nhẹ rồi tiếp tục — không phải một cú cuộn duy nhất như máy.
    const int steps = Utils::randomInt(3, 7);
    for (int i = 0; i < steps; ++i) {
        const int delta = Utils::randomInt(80, 420);
        // ~20% số bước: cuộn ngược lại một chút như người lỡ cuộn quá.
        const int dir = (i > 0 && Utils::randomInt(0, 4) == 0) ? -1 : 1;
        evaluate(QStringLiteral("(function(){window.scrollBy({top:%1,behavior:'smooth'});return true;})()")
                     .arg(delta * dir));
        Utils::sleepMs(Utils::randomInt(90, 400));
    }
}

QJsonArray WebDriver::queryAll(const QString &xpath, int timeoutMs)
{
    const QString js = QStringLiteral(
        "(function(){var ns=window.__apxpath(%1);return ns.map(function(n){"
        "var s=n.querySelector('span[dir=\"auto\"]');"
        "return {"
        "text:(n.innerText||n.textContent||'').trim(),"
        "href:(n.getAttribute('href')||''),"
        "aria:(n.getAttribute('aria-label')||''),"
        "auto:(s?(s.innerText||s.textContent||'').trim():''),"
        "parent:(n.parentElement?((n.parentElement.innerText||n.parentElement.textContent||'').toLowerCase()):'')"
        "};});})()")
        .arg(jsStr(xpath));
    const QJsonValue v = evaluate(js, timeoutMs);
    return v.isArray() ? v.toArray() : QJsonArray();
}

int WebDriver::count(const QString &xpath)
{
    return evaluate(QStringLiteral("window.__apxpath(%1).length").arg(jsStr(xpath))).toInt();
}

int WebDriver::visibleCount(const QString &xpath)
{
    const QString js = QStringLiteral(
        "window.__apxpath(%1).filter(function(n){"
        "return n.offsetParent!==null&&!n.disabled;}).length")
        .arg(jsStr(xpath));
    return evaluate(js).toInt();
}

bool WebDriver::clickNth(const QString &xpath, int index)
{
    const QString js = QStringLiteral(
        "(function(){var ns=window.__apxpath(%1);var el=ns[%2];if(!el)return false;"
        "el.scrollIntoView({block:'center'});"
        "['mouseover','mousedown','mouseup'].forEach(function(t){"
        "el.dispatchEvent(new MouseEvent(t,{bubbles:true}));});"
        "el.click();return true;})()")
        .arg(jsStr(xpath), QString::number(index));
    return evaluate(js).toBool();
}

QJsonObject WebDriver::elementRect(const QString &xpath, int index)
{
    const QString js = QStringLiteral(
        "(function(){var ns=window.__apxpath(%1);var el=ns[%2];if(!el)return {};"
        "var r=el.getBoundingClientRect();"
        "if(r.width<1&&r.height<1)return {};"
        "return {x:r.left+r.width/2,y:r.top+r.height/2,"
        "cx:r.left+Math.random()*r.width,cy:r.top+Math.random()*r.height};})()")
        .arg(jsStr(xpath), QString::number(index));
    return evaluate(js).toObject();
}

void WebDriver::humanMouseMove(double toX, double toY)
{
    if (!m_cdp)
        return;
    // Đường cong Bezier bậc 2 từ điểm xuất phát ngẫu nhiên gần đích, điểm điều
    // khiển lệch ngẫu nhiên mỗi lần → quỹ đạo chuột khác nhau từng lần như người.
    const double startX = toX + Utils::randomInt(-70, 70);
    const double startY = toY + Utils::randomInt(-50, 50);
    const double c1x = (startX + toX) / 2 + Utils::randomInt(-45, 45);
    const double c1y = (startY + toY) / 2 + Utils::randomInt(-35, 35);
    const int steps = Utils::randomInt(10, 18);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double mt = 1.0 - t;
        const double x = mt * mt * startX + 2 * mt * t * c1x + t * t * toX;
        const double y = mt * mt * startY + 2 * mt * t * c1y + t * t * toY;
        // Fire-and-forget: Input CDP xử lý FIFO trên cùng kết nối nên thứ tự vẫn
        // đảm bảo, nhưng bỏ được 10-18 lượt chờ phản hồi mỗi cú click -> nhanh hơn.
        m_cdp->sendCommand(QStringLiteral("Input.dispatchMouseEvent"),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("mouseMoved")},
                                       {QStringLiteral("x"), x},
                                       {QStringLiteral("y"), y},
                                       {QStringLiteral("buttons"), 0}},
                           30000, false);
        Utils::sleepMs(Utils::randomInt(4, 16));
    }
}

bool WebDriver::realClick(const QString &xpath, int index)
{
    if (!m_cdp)
        return false;
    // Trước đây: elementRect → scroll + sleep → elementRect lại = 3 evaluate + chờ.
    // Nay gộp: đánh giá một lần — cuộn vào giữa, chờ 1 khung hình (rAF x2) cho ổn
    // định, rồi trả tọa độ mới luôn. Chỉ còn 1 round-trip CDP cho mỗi lượt click.
    const QString js = QStringLiteral(
        "(async function(){var ns=window.__apxpath(%1);var el=ns[%2];if(!el)return null;"
        "el.scrollIntoView({block:'center'});"
        "await new Promise(function(r){requestAnimationFrame(function(){requestAnimationFrame(r);});});"
        "await new Promise(function(r){setTimeout(r,120);});"
        "var r=el.getBoundingClientRect();"
        "if(r.width<1&&r.height<1)return null;"
        "return {x:r.left+r.width/2,y:r.top+r.height/2,"
        "cx:r.left+Math.random()*r.width,cy:r.top+Math.random()*r.height};})()")
                           .arg(jsStr(xpath), QString::number(index));
    const QJsonValue v = evaluate(js, 5000);
    const QJsonObject r = v.toObject();
    if (r.isEmpty())
        return false;
    const double x = r.value(QStringLiteral("cx")).toDouble();
    const double y = r.value(QStringLiteral("cy")).toDouble();

    // Di chuột tới rồi bấm bằng sự kiện chuột thật qua CDP.
    humanMouseMove(x, y);
    Utils::sleepMs(Utils::randomInt(40, 110));

    QJsonObject press{{QStringLiteral("type"), QStringLiteral("mousePressed")},
                      {QStringLiteral("x"), x},
                      {QStringLiteral("y"), y},
                      {QStringLiteral("button"), QStringLiteral("left")},
                      {QStringLiteral("buttons"), 1},
                      {QStringLiteral("clickCount"), 1}};
    m_cdp->sendCommand(QStringLiteral("Input.dispatchMouseEvent"), press);
    Utils::sleepMs(Utils::randomInt(50, 140));
    QJsonObject release{{QStringLiteral("type"), QStringLiteral("mouseReleased")},
                        {QStringLiteral("x"), x},
                        {QStringLiteral("y"), y},
                        {QStringLiteral("button"), QStringLiteral("left")},
                        {QStringLiteral("buttons"), 0},
                        {QStringLiteral("clickCount"), 1}};
    m_cdp->sendCommand(QStringLiteral("Input.dispatchMouseEvent"), release);
    return true;
}

bool WebDriver::selectCombobox(int comboboxIndex, const QString &value)
{
    if (!m_cdp)
        return false;

    // 0) Listbox của Facebook dùng CHUNG một portal: nếu dropdown trước còn
    //    mở, click vào combobox khác chỉ đóng dropdown cũ chứ không mở dropdown
    //    mới. Bấm Escape để chắc chắn không còn listbox nào treo.
    pressEscape();
    Utils::sleepMs(Utils::randomInt(150, 300));

    // 1) Mở dropdown bằng chuột thật (realClick tự cuộn + đọc lại tọa độ nên
    //    không bị lệch khi trang cuộn sau mỗi lần chọn).
    if (!realClick(QStringLiteral("//*[@role='combobox']"), comboboxIndex))
        return false;
    Utils::sleepMs(Utils::randomInt(400, 800));

    // 2) Chờ option khớp xuất hiện (chỉ tính option đang hiển thị — portal dùng
    //    chung cho cả 4 dropdown nên option ẩn phải được lọc bằng offsetParent).
    const QString matchJs =
        QStringLiteral("window.__apxpath(\"//*[@role='option']\").some(function(o){"
                       "return o.offsetParent!==null&&(o.innerText||o.textContent||'').trim()===%1;})")
            .arg(jsStr(value));
    if (!waitForCondition(matchJs, 5000))
        return false;

    // 3) Cuộn option vào giữa rồi lấy lại tọa độ (listbox cuộn được, option ở
    //    dưới màn hình phải cuộn lên trước khi bấm).
    const QString scrollJs =
        QStringLiteral("(function(){var ns=window.__apxpath(\"//*[@role='option']\");"
                       "for(var i=0;i<ns.length;i++){var o=ns[i];"
                       "if(o.offsetParent!==null&&(o.innerText||o.textContent||'').trim()===%1){"
                       "o.scrollIntoView({block:'center'});return true;}}return false;})()")
            .arg(jsStr(value));
    if (!evaluate(scrollJs).toBool())
        return false;
    Utils::sleepMs(Utils::randomInt(250, 450));

    const QString rectJs =
        QStringLiteral("(function(){var ns=window.__apxpath(\"//*[@role='option']\");"
                       "for(var i=0;i<ns.length;i++){var o=ns[i];"
                       "if(o.offsetParent!==null&&(o.innerText||o.textContent||'').trim()===%1){"
                       "var r=o.getBoundingClientRect();"
                       "return JSON.stringify({x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2)});"
                       "}}return '';})()")
            .arg(jsStr(value));
    const QJsonObject rect =
        QJsonDocument::fromJson(evaluate(rectJs).toString().toUtf8()).object();
    const double x = rect.value(QStringLiteral("x")).toDouble();
    const double y = rect.value(QStringLiteral("y")).toDouble();
    if (x <= 0 || y <= 0)
        return false;

    // 4) Bấm chuột thật tại tọa độ option (React của Facebook chỉ nhận sự kiện
    //    chuột thật, JS .click() không chọn được).
    m_cdp->sendCommand(QStringLiteral("Input.dispatchMouseEvent"),
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("mousePressed")},
                                   {QStringLiteral("x"), x},
                                   {QStringLiteral("y"), y},
                                   {QStringLiteral("button"), QStringLiteral("left")},
                                   {QStringLiteral("buttons"), 1},
                                   {QStringLiteral("clickCount"), 1}});
    Utils::sleepMs(Utils::randomInt(50, 120));
    m_cdp->sendCommand(QStringLiteral("Input.dispatchMouseEvent"),
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("mouseReleased")},
                                   {QStringLiteral("x"), x},
                                   {QStringLiteral("y"), y},
                                   {QStringLiteral("button"), QStringLiteral("left")},
                                   {QStringLiteral("buttons"), 0},
                                   {QStringLiteral("clickCount"), 1}});
    return true;
}

bool WebDriver::setContentEditableText(const QString &xpath, const QString &text)
{
    const QString js = QStringLiteral(
        "(function(){var el=window.__apxpath(%1)[0];if(!el)return false;"
        "el.focus();document.execCommand('selectAll',false,null);document.execCommand('delete',false,null);"
        "%2.split('\\n').forEach(function(l,i,a){"
        "document.execCommand('insertText',false,l);"
        "if(i<a.length-1)document.execCommand('insertLineBreak',false,null);});"
        "el.dispatchEvent(new Event('input',{bubbles:true}));"
        "el.dispatchEvent(new Event('change',{bubbles:true}));return true;})()")
        .arg(jsStr(xpath), jsStr(text));
    return evaluate(js).toBool();
}

bool WebDriver::typeText(const QString &xpath, const QString &text)
{
    // Chuẩn bị: focus + xóa nội dung cũ.
    const QString prep = QStringLiteral(
        "(function(){var el=window.__apxpath(%1)[0];if(!el)return false;"
        "el.focus();document.execCommand('selectAll',false,null);document.execCommand('delete',false,null);"
        "el.dispatchEvent(new Event('input',{bubbles:true}));return true;})()")
        .arg(jsStr(xpath));
    if (!evaluate(prep).toBool())
        return false;

    const QString insertText = QStringLiteral(
        "(function(){var el=window.__apxpath(%1)[0];if(!el)return false;"
        "el.focus();document.execCommand('insertText',false,%2);"
        "el.dispatchEvent(new Event('input',{bubbles:true}));return true;})()");

    // Chuẩn hóa dòng mới: quy về \n để tránh ký tự \r bị gõ thành ký tự lạ.
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = t.split(QLatin1Char('\n'));
    for (int li = 0; li < lines.size(); ++li) {
        const QString line = lines.at(li);
        // Gõ theo chunk 2-7 ký tự: 1 lệnh CDP/chunk thay vì 1 lệnh/ký tự
        // (giảm ~4 lần round-trip CDP -> nhanh hơn hẳn ở bài dài), nhịp nghỉ
        // vẫn tỉ lệ với số ký tự nên tốc độ gõ tổng thể vẫn giống người.
        for (int ci = 0; ci < line.size();) {
            const int n = qMin(Utils::randomInt(2, 7), line.size() - ci);
            if (!evaluate(insertText.arg(jsStr(xpath), jsStr(line.mid(ci, n)))).toBool())
                return false;
            ci += n;
            Utils::sleepMs(Utils::randomInt(15 * n, 70 * n));
            // Thỉnh thoảng dừng lâu hơn (nghĩ từ) — giữ dáng hành vi.
            if (Utils::randomInt(0, 3) == 0)
                Utils::sleepMs(Utils::randomInt(40, 220));
        }
        if (li < lines.size() - 1) {
            if (!pressEnter())
                return false;
            Utils::humanPause(80, 300);
        }
    }
    return true;
}

bool WebDriver::pasteText(const QString &xpath, const QString &text)
{
    if (!m_cdp)
        return false;

    // Focus + xóa nội dung cũ.
    const QString prep = QStringLiteral(
        "(function(){var el=window.__apxpath(%1)[0];if(!el)return false;"
        "el.focus();document.execCommand('selectAll',false,null);document.execCommand('delete',false,null);"
        "el.dispatchEvent(new Event('input',{bubbles:true}));return true;})()")
        .arg(jsStr(xpath));
    if (!evaluate(prep).toBool())
        return false;

    // Đặt nội dung vào clipboard của trang qua Clipboard API (awaitPromise được bật).
    const QString setClip = QStringLiteral(
        "(async function(){try{"
        "var el=window.__apxpath(%1)[0];if(el)el.focus();"
        "await navigator.clipboard.writeText(%2);return true;"
        "}catch(e){return false;}})()")
        .arg(jsStr(xpath), jsStr(text));
    if (!evaluate(setClip).toBool())
        return false;

    // Dán bằng phím Ctrl+V thật qua CDP; Facebook xử lý dán như người dùng
    // (giữ đúng số dòng, không bị cách dòng đôi).
    const QJsonObject ctrlDown{
        {QStringLiteral("type"), QStringLiteral("rawKeyDown")},
        {QStringLiteral("modifiers"), 2}, // 2 = Ctrl
        {QStringLiteral("key"), QStringLiteral("Control")},
        {QStringLiteral("code"), QStringLiteral("ControlLeft")},
        {QStringLiteral("windowsVirtualKeyCode"), 17},
        {QStringLiteral("nativeVirtualKeyCode"), 17},
    };
    const QJsonObject vDown{
        {QStringLiteral("type"), QStringLiteral("rawKeyDown")},
        {QStringLiteral("modifiers"), 2},
        {QStringLiteral("key"), QStringLiteral("v")},
        {QStringLiteral("code"), QStringLiteral("KeyV")},
        {QStringLiteral("windowsVirtualKeyCode"), 86},
        {QStringLiteral("nativeVirtualKeyCode"), 86},
    };
    const QJsonObject vUp{
        {QStringLiteral("type"), QStringLiteral("keyUp")},
        {QStringLiteral("modifiers"), 2},
        {QStringLiteral("key"), QStringLiteral("v")},
        {QStringLiteral("code"), QStringLiteral("KeyV")},
        {QStringLiteral("windowsVirtualKeyCode"), 86},
        {QStringLiteral("nativeVirtualKeyCode"), 86},
    };
    const QJsonObject ctrlUp{
        {QStringLiteral("type"), QStringLiteral("keyUp")},
        {QStringLiteral("modifiers"), 0},
        {QStringLiteral("key"), QStringLiteral("Control")},
        {QStringLiteral("code"), QStringLiteral("ControlLeft")},
        {QStringLiteral("windowsVirtualKeyCode"), 17},
        {QStringLiteral("nativeVirtualKeyCode"), 17},
    };
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), ctrlDown);
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), vDown);
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), vUp);
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), ctrlUp);
    return true;
}

bool WebDriver::pressEnter()
{
    if (!m_cdp)
        return false;
    const QJsonObject down{
        {QStringLiteral("type"), QStringLiteral("keyDown")},
        {QStringLiteral("key"), QStringLiteral("Enter")},
        {QStringLiteral("code"), QStringLiteral("Enter")},
        {QStringLiteral("windowsVirtualKeyCode"), 13},
        {QStringLiteral("nativeVirtualKeyCode"), 13},
        {QStringLiteral("text"), QStringLiteral("\r")},
        {QStringLiteral("unmodifiedText"), QStringLiteral("\r")},
    };
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), down);
    const QJsonObject up{
        {QStringLiteral("type"), QStringLiteral("keyUp")},
        {QStringLiteral("key"), QStringLiteral("Enter")},
        {QStringLiteral("code"), QStringLiteral("Enter")},
        {QStringLiteral("windowsVirtualKeyCode"), 13},
        {QStringLiteral("nativeVirtualKeyCode"), 13},
    };
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), up);
    return true;
}

bool WebDriver::pressEscape()
{
    if (!m_cdp)
        return false;
    const QJsonObject down{
        {QStringLiteral("type"), QStringLiteral("keyDown")},
        {QStringLiteral("key"), QStringLiteral("Escape")},
        {QStringLiteral("code"), QStringLiteral("Escape")},
        {QStringLiteral("windowsVirtualKeyCode"), 27},
        {QStringLiteral("nativeVirtualKeyCode"), 27},
    };
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), down);
    const QJsonObject up{
        {QStringLiteral("type"), QStringLiteral("keyUp")},
        {QStringLiteral("key"), QStringLiteral("Escape")},
        {QStringLiteral("code"), QStringLiteral("Escape")},
        {QStringLiteral("windowsVirtualKeyCode"), 27},
        {QStringLiteral("nativeVirtualKeyCode"), 27},
    };
    m_cdp->sendCommand(QStringLiteral("Input.dispatchKeyEvent"), up);
    return true;
}

bool WebDriver::uploadFiles(const QString &cssSelector, const QStringList &paths)
{
    if (!m_cdp)
        return false;

    m_cdp->sendCommand(QStringLiteral("DOM.enable"));
    const QJsonObject doc = m_cdp->sendCommand(QStringLiteral("DOM.getDocument"), {{QStringLiteral("depth"), 0}});
    const int rootId = doc.value(QStringLiteral("result")).toObject()
                           .value(QStringLiteral("root")).toObject()
                           .value(QStringLiteral("nodeId")).toInt();
    if (rootId <= 0)
        return false;

    const QJsonObject q = m_cdp->sendCommand(QStringLiteral("DOM.querySelector"),
                                             {{QStringLiteral("nodeId"), rootId},
                                              {QStringLiteral("selector"), cssSelector}});
    const int nodeId = q.value(QStringLiteral("result")).toObject().value(QStringLiteral("nodeId")).toInt();
    if (nodeId <= 0)
        return false;

    QJsonArray files;
    for (const QString &p : paths)
        files.append(p);

    const QJsonObject res = m_cdp->sendCommand(QStringLiteral("DOM.setFileInputFiles"),
                                               {{QStringLiteral("nodeId"), nodeId},
                                                {QStringLiteral("files"), files}});
    return !res.contains(QStringLiteral("error"));
}
