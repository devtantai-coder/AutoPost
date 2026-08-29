#pragma once

#include <QVector>

#include "model/FacebookGroup.h"

class WebDriver;

namespace FacebookParser
{
QVector<FacebookGroup> extractMyGroups(WebDriver *d);
QVector<FacebookGroup> extractFromPage(WebDriver *d, bool onlyMember);
} // namespace FacebookParser
