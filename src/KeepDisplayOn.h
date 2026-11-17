/*********************************************************************************/
/*!
@file           KeepDisplayOn.h

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

#ifndef __KEEP_DISPLAY_ON_H__
#define __KEEP_DISPLAY_ON_H__

// The operating system only sees the computer keyboard and mouse as user
// activity, not a MIDI keyboard, so the display may turn off while the user is
// playing the piano. These functions tell the operating system that the
// display is still in use.
//
// They must always be called from the same (main) thread.
// Currently only implemented on Windows; on other systems they do nothing.
class CKeepDisplayOn
{
public:
    // Keep the display on until called again with false (used while a song is playing).
    static void setRequired(bool required);

    // Reset the system's idle timer, as if the user had moved the mouse
    // (used when a note is played on the MIDI keyboard).
    static void userActivity();
};

#endif // __KEEP_DISPLAY_ON_H__
