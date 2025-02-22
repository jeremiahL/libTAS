/*
    Copyright 2015-2020 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "LuaFunctionList.h"
#include "Main.h"
#include "../utils.h"
#include <iostream>
#include <unistd.h>
#include <sys/inotify.h>
#include <cerrno>

namespace Lua {

LuaFunctionList::LuaFunctionList() {
    inotifyfd = inotify_init1(IN_NONBLOCK);
    if (inotifyfd < 0)
        std::cerr << "inotify_init() failed with error " << errno << std::endl;
}

LuaFunctionList::~LuaFunctionList() {
    close(inotifyfd);
}

void LuaFunctionList::add(lua_State *L, NamedLuaFunction::CallbackType t)
{
    functions.emplace_back(L, t);
}

void LuaFunctionList::addFile(const std::string& file)
{
    if (fileSet.find(file) != fileSet.end())
        return;

    int ret = Lua::Main::run(file);
        
    if (ret < 0)
        return;

    LuaFile f;
    f.file = file;
    f.filename = fileFromPath(file);
    /* Register inotify on file to look for changes */
    f.wd = inotify_add_watch(inotifyfd, file.c_str(), IN_MODIFY);
    f.enabled = true;

    fileSet.insert(file);
    fileList.push_back(f);
}

void LuaFunctionList::removeForFile(int row)
{
    const std::string& file = fileList[row].file;
    functions.remove_if([&file](const NamedLuaFunction& nlf){ return 0 == file.compare(nlf.file); });

    inotify_rm_watch(inotifyfd, fileList[row].wd);
    fileSet.erase(file);
    fileList.erase(fileList.begin() + row);
}

void LuaFunctionList::watchChanges()
{
    struct inotify_event ev;
    int ret = read(inotifyfd, &ev, sizeof(struct inotify_event));
    if (ret < 0) {
        if (errno != EAGAIN) // no event
            std::cerr << "Error reading inotify event: " << errno << std::endl;
    }
    else {
        for (size_t i = 0; i < fileList.size(); i++) {
            if (fileList[i].wd == ev.wd ) {
                if (ev.mask == IN_MODIFY) {
                    /* Reload the file */
                    const std::string& file = fileList[i].file;
                    functions.remove_if([&file](const NamedLuaFunction& nlf){ return 0 == file.compare(nlf.file); });
                    Lua::Main::run(file);
                }
                return;
            }
        }
    }
}

bool LuaFunctionList::activeState(int row) const
{
    return fileList[row].enabled;
}

void LuaFunctionList::switchForFile(int row, bool active)
{
    fileList[row].enabled = active;
    const std::string& file = fileList[row].file;
    for (auto& nlf : functions) {
        if (0 == file.compare(nlf.file)) {
            nlf.active = active;
        }
    }
}

void LuaFunctionList::call(NamedLuaFunction::CallbackType c)
{
    for (auto& nlf : functions) {
        if (nlf.active && nlf.type == c) {
            nlf.call();
        }
    }
}

int LuaFunctionList::fileCount() const
{
    return fileSet.size();
}

void LuaFunctionList::clear()
{
    functions.clear();
    fileList.clear();
    fileSet.clear();
}

}
