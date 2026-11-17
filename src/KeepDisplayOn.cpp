/*********************************************************************************/
/*!
@file           KeepDisplayOn.cpp

@brief          Stops the display from turning off while Piano Booster is in use.

    PianoBooster is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PianoBooster is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PianoBooster.  If not, see <http://www.gnu.org/licenses/>.

*/
/*********************************************************************************/

#include "KeepDisplayOn.h"

#include <QElapsedTimer>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace {
    bool s_required = false;

    // The idle timer only needs resetting now and then, not on every note.
    constexpr qint64 USER_ACTIVITY_INTERVAL_MS = 10000;
}

void CKeepDisplayOn::setRequired(bool required)
{
    if (required == s_required)
        return;
    s_required = required;

#ifdef _WIN32
    // ES_CONTINUOUS keeps the state until it is changed again; calling it
    // with only ES_CONTINUOUS clears the request.
    if (required)
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    else
        SetThreadExecutionState(ES_CONTINUOUS);
#endif
}

void CKeepDisplayOn::userActivity()
{
    static QElapsedTimer lastActivity;
    if (lastActivity.isValid() && lastActivity.elapsed() < USER_ACTIVITY_INTERVAL_MS)
        return;
    lastActivity.start();

#ifdef _WIN32
    // Without ES_CONTINUOUS this just resets the display and system idle timers.
    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
#endif
}
