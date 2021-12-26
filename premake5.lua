-- premake5.lua
-- version: premake-5.0.0-alpha14

-- Patch premake to link shared libraries on Linux with the -rpath='$ORIGIN' flag. This makes
-- libraries load relative to the location of the library that is pulling them in instead of from an
-- absolute location on disk.
--
-- References:
--
-- * [correct usage of rpath (relative vs absolute)](https://stackoverflow.com/questions/38058041/correct-usage-of-rpath-relative-vs-absolute)
-- * [LD_LIBRARY_PATH – or: How to get yourself into trouble!](https://www.hpc.dtu.dk/?page_id=1180)
old_shared_lib = premake.tools.clang.ldflags.kind.SharedLib
premake.tools.clang.ldflags.kind.SharedLib = function(cfg)
    if cfg.system == premake.LINUX then
        local r = { premake.tools.clang.getsharedlibarg(cfg) }
        table.insert(r, '-Wl,-soname=' .. premake.quoted(cfg.linktarget.name) .. ',-rpath=\'$$ORIGIN\'')
        return r
    else
        return old_shared_lib(cfg)
    end
end

-- override the premake build-in preprocessor flags from -MMD to -MD
old_getcppflags = premake.tools.clang.getcppflags
premake.tools.clang.getcppflags = function(cfg)
    if cfg.system == premake.LINUX then
        return {"-MD","-MP"}
    else
        return old_getcppflags(cfg)
    end
end
-- Check if this Linux build is using WSL, in that case we will skip certain parts of the build,
-- such as XWindows.
--
-- Reference: https://stackoverflow.com/questions/38086185/how-to-check-if-a-program-is-run-in-bash-on-ubuntu-on-windows-and-not-just-plain
running_under_wsl = false
local file = io.open("/proc/version", "r")
if file then
    local content = file:read("*a")
    if string.find(content, "Microsoft") or string.find(content, "microsoft") or string.find(content, "WSL") then
        running_under_wsl = true
    end
    file:close()
end

newoption {
    trigger     = "clang",
    description = "Force use of CLANG for Windows builds"
 }

function win_copy_to_plugins(from)
    return 'XCOPY "' .. from:gsub("/", "\\") .. '" "..\\..\\..\\bin\\%{cfg.buildcfg}\\plugins\\" /Q/Y/I/C '
end

function win_copy_to_bin(from)
    return 'XCOPY "' .. from:gsub("/", "\\") .. '" "..\\..\\..\\bin\\%{cfg.buildcfg}\\" /Q/Y/I/C '
end

-- Returns a snake-case version of the kebab-case string `name`.
function snake_case(name)
    return string.gsub(name, "-", "_")
end

-- Include all project files from specified folder
function folder(t)
    if type(t) ~= "table" then t = {t} end
    for _,f in ipairs(t) do
        files {f .. "/**.h",  f .. "/**.c", f .. "/**.inl", f .. "/**.cpp", f .. "/**.m", f .. "/**.tmsl"}
    end
end

-- Make incluedirs() also call sysincludedirs()
oldincludedirs = includedirs
function includedirs(dirs)
    oldincludedirs(dirs)
    sysincludedirs(dirs)
end

function find_shader_data_dir(shader_path)
    i = string.find(shader_path, "shaders")
    dir = "shaders"
    if i ~= nil then
        dir = string.sub(shader_path, i, -1)
    end
    return dir
end

function find_data_parent_dir(target_path, build_cfg)
    i, j = string.find(target_path, build_cfg)
    dir = ""
    if i ~= nil then
     dir = string.sub(target_path, 1, j + 1)
    else
        dir = target_path
    end
    return dir
end

function copy_shaders_to_data_dir()
    filter 'files:**.tmsl'
        buildcommands {
            'echo Copying "%{file.name} to %{find_data_parent_dir(cfg.buildtarget.abspath, cfg.buildcfg)}/data/%{find_shader_data_dir(file.directory)}/%{file.name}"',
            '{MKDIR} %{find_data_parent_dir(cfg.buildtarget.abspath, cfg.buildcfg)}/data/%{find_shader_data_dir(file.directory)}',
            '{COPY} %{file.abspath} %{find_data_parent_dir(cfg.buildtarget.abspath, cfg.buildcfg)}/data/%{find_shader_data_dir(file.directory)}',
        }

        buildoutputs '%{find_data_parent_dir(cfg.buildtarget.abspath, cfg.buildcfg)}/data/%{find_shader_data_dir(file.directory)}/%{file.name}'
        
    filter {}
end
-- Base for all projects
function base(name)
    project(name)
        language "C++"
        includedirs { "" }
end

function lib_path(path)
    local lib_dir = os.getenv("TM_LIB_DIR")

    if lib_dir == nil then
        error("TM_LIB_DIR not set")
        return nil
    end

    return lib_dir .. "/" .. path
end

-- Project type for plugins with web support
function webplugin(name)
    local sn = snake_case(name)
    base(name)
        location("build/plugins/" .. sn)
        filter { "platforms:web" }
            kind "StaticLib"
        filter { "not platforms:web" }
            kind "SharedLib"
        filter {}
        targetdir "bin/%{cfg.buildcfg}/plugins"
        targetname("tm_" .. sn)
        defines {"TM_LINKS_" .. string.upper(sn)}
        dependson("foundation")
        folder {"plugins/" .. sn}
        copy_shaders_to_data_dir()
end

-- Project type for plugins
function plugin(name)
    webplugin(name)
        removeplatforms { "web" }
end

-- Project type for utility programs
function util(name)
    local sn = snake_case(name)
    base(name)
        location("build/" .. sn)
        kind "ConsoleApp"
        targetdir "bin/%{cfg.buildcfg}"
        defines { "TM_LINKS_FOUNDATION" }
        dependson { "foundation" }
        links { "foundation" }
        folder {"utils/" .. sn}
        filter { "platforms:Linux" }
            linkoptions {"-ldl", "-lanl", "-pthread"}
        filter {"platforms:web"}
            targetextension ".html"
        filter {} -- clear filter for future calls
end

function main_dll(t)
    if type(t) == "string" then t = {name=t} end
    local sn = snake_case(t.name)
    base(t.name .. "-dll")
        removeplatforms { "web" }
        location("build/" .. sn .. "_dll")
        kind "SharedLib"
        targetdir "bin/%{cfg.buildcfg}"
        defines {"TM_LINKS_" .. string.upper(sn)}

        local dir = t.dir or sn
        folder {dir}
        removefiles {dir .. "/host.c"}

        filter { "platforms:Win64" }
            dependson {"render-backend-vulkan"}
            links { "Shcore.lib" }
        filter {}


end

function main_exe(t, copy_data)
    if type(t) == "string" then t = {name=t} end
    local sn = snake_case(t.name)
    base(t.name .. "-exe")
        removeplatforms { "web" }
        location("build/" .. sn .. "_exe")
        targetname(t.name)
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
        language "C++"
        targetdir "bin/%{cfg.buildcfg}"
        defines { "TM_LINKS_FOUNDATION" }
        dependson {"foundation", t.name .. "-dll"}
        links {"foundation"}
        local dir = t.dir or sn
        files {dir .. "/host.c"}

        -- Only true for the-machinery.exe since multiple projects doing this with a multi-threaded build
        -- leads to access violation race conditions.
        if copy_data then
            postbuildcommands {
                '{MKDIR} ../../bin/%{cfg.buildcfg}/plugins', 
                '{MKDIR} ../../bin/%{cfg.buildcfg}/data/shaders', 
                '{MKDIR} ../../bin/%{cfg.buildcfg}/data/core',
                '{MKDIR} ../../bin/%{cfg.buildcfg}/data/fonts',
                '{MKDIR} ../../bin/%{cfg.buildcfg}/data/luts/512',
                '{MKDIR} ../../bin/%{cfg.buildcfg}/data/templates'
            }
            -- {COPY} behavior for folders is inconsistent in premake. On Windows, copies "into" the
            -- folder. On Linux/OS X, copies folder.
            filter { "platforms:Win64" }
                postbuildcommands {
                    '{COPY} "%TM_SDK_DIR%/bin/Debug/plugins" ../../bin/%{cfg.buildcfg}/plugins',
                    '{COPY} "%TM_SDK_DIR%/bin/data/shaders" ../../bin/%{cfg.buildcfg}/data/shaders',
                    '{COPY} "%TM_SDK_DIR%/bin/data/fonts" ../../bin/%{cfg.buildcfg}/data/fonts',
                    '{COPY} "%TM_SDK_DIR%/bin/data/luts" ../../bin/%{cfg.buildcfg}/data/luts'
                }
            filter { "platforms:Linux or MacOSX-x64 or MacOSX-ARM" }
                postbuildcommands {
                    '{COPY} "${TM_SDK_DIR}/bin/Debug/plugins" ../../bin/%{cfg.buildcfg}/',
                    '{COPY} "${TM_SDK_DIR}/bin/Debug/data/shaders" ../../bin/%{cfg.buildcfg}/data/',
                    '{COPY} "${TM_SDK_DIR}/bin/Debug/data/fonts" ../../bin/%{cfg.buildcfg}/data/',
                    '{COPY} "${TM_SDK_DIR}/bin/Debug/data/luts" ../../bin/%{cfg.buildcfg}/data/'
                }
            filter {}
        end
        
        filter { "platforms:Win64" }
            files {dir .. "/*.rc"}
        filter { "platforms:Linux" }
            linkoptions {"-ldl", "-lanl", "-pthread"}
        filter {}
end


workspace "the-machinery"
    configurations {"Debug", "Release"}
    language "C++"
    cppdialect "C++11"
    filter { "options:not clang" }
        flags {"MultiProcessorCompile" }
    filter {}
    flags { "FatalWarnings" }
    warnings "Extra"
    inlining "Auto"
    editandcontinue "Off"
    -- Enable this to test compile with strict aliasing.
    -- strictaliasing "Level3"
    -- Enable this to report build times, see: http://aras-p.info/blog/2019/01/21/Another-cool-MSVC-flag-d1reportTime/
    -- buildoptions { "/Bt+", "/d2cgsummary", "/d1reportTime" }

filter { "system:windows" }
    platforms { "Win64", "web" }
    systemversion("latest")

filter { "system:windows", "options:clang" }
    toolset("msc-clangcl")
    buildoptions {
        "-Wno-missing-field-initializers",   -- = {0} is OK.
        "-Wno-unused-parameter",             -- Useful for documentation purposes.
        "-Wno-unused-local-typedef",         -- We don't always use all typedefs.
        "-Wno-missing-braces",               -- = {0} is OK.
        "-Wno-microsoft-anon-tag",           -- Allow anonymous structs.
        "-Wshadow",                          -- Warn about shadowed variables.
        "-Wpadded",                           -- Require explicit padding.
        "-fcommon",                          -- Allow tentative definitions
    }
    buildoptions {
        "-fms-extensions",                   -- Allow anonymous struct as C inheritance.
        "-mavx",                             -- AVX.
        "-mfma",                             -- FMA.
    }

    removeflags {"FatalLinkWarnings"}        -- clang linker doesn't understand /WX

filter { "system:macosx" }
    platforms { "MacOSX-x64", "MacOSX-ARM", "web" }

filter {"system:linux"}
    platforms { "Linux", "web" }

filter { "platforms:Win64" }
    defines { "TM_OS_WINDOWS", "_CRT_SECURE_NO_WARNINGS" }
    staticruntime "On"
    architecture "x64"
    defines {"TM_CPU_X64", "TM_CPU_AVX", "TM_CPU_SSE"}
    buildoptions {
        "/we4121", -- Padding was added to align structure member on boundary.
        "/we4820", -- Padding was added to end of struct.
        "/utf-8",  -- Source encoding is UTF-8.
    }
    disablewarnings {
        "4057", -- Slightly different base types. Converting from type with volatile to without.
        "4100", -- Unused formal parameter. I think unusued parameters are good for documentation.
        "4152", -- Conversion from function pointer to void *. Should be ok.
        "4200", -- Zero-sized array. Valid C99.
        "4201", -- Nameless struct/union. Valid C11.
        "4204", -- Non-constant aggregate initializer. Valid C99.
        "4206", -- Translation unit is empty. Might be #ifdefed out.
        "4214", -- Bool bit-fields. Valid C99.
        "4221", -- Pointers to locals in initializers. Valid C99.
        "4702", -- Unreachable code. We sometimes want return after exit() because otherwise we get an error about no return value.
        "4828", -- Strange characters in comments. The Steam SDK header files have some.
    }
    -- ITERATOR_DEBUG_LEVEL kills performance, we don't want it in our builds.
    defines {'_ITERATOR_DEBUG_LEVEL=0'}

filter {"platforms:MacOSX-x64"}
    architecture "x64"
    defines {"TM_CPU_X64", "TM_CPU_AVX", "TM_CPU_SSE"}
    buildoptions {
        "-mavx",                            -- AVX.
        "-mfma"                             -- FMA.
    }

filter {"platforms:MacOSX-ARM"}
    architecture "ARM"
    defines {"TM_CPU_ARM", "TM_CPU_NEON"}

filter { "platforms:MacOSX-x64 or MacOSX-ARM" }
    defines { "TM_OS_MACOSX", "TM_OS_POSIX", "TM_NO_MAIN_FIBER" }
    buildoptions {
        "-fms-extensions",                   -- Allow anonymous struct as C inheritance.
    }
    enablewarnings {
        "shadow",
         "padded"
    }
    disablewarnings {
        "missing-field-initializers",   -- = {0} is OK.
        "unused-parameter",             -- Useful for documentation purposes.
        "unused-local-typedef",         -- We don't always use all typedefs.
        "missing-braces",               -- = {0} is OK.
        "microsoft-anon-tag",           -- Allow anonymous structs.
    }
    -- Needed, because Xcode project generation does not respect `disablewarnings` (premake-5.0.0-alpha13)
    xcodebuildsettings {
        WARNING_CFLAGS = "-Wall -Wextra " ..
            "-Wno-missing-field-initializers " ..
            "-Wno-unused-parameter " ..
            "-Wno-unused-local-typedef " ..
            "-Wno-missing-braces " ..
            "-Wno-microsoft-anon-tag "
    }

filter { "platforms:web" }
    defines { "TM_OS_WEB", "TM_OS_POSIX", "TM_NO_MAIN_FIBER" }
    buildoptions {
        "-fms-extensions",                   -- Allow anonymous struct as C inheritance.
    }
    enablewarnings {
        "shadow",
        -- Emscripten uses 32-bit pointers so the struct layout ends up different and generates
        -- lots of warnings if "padded" is enabled.
        -- "padded"
    }
    disablewarnings {
        "missing-field-initializers",   -- = {0} is OK.
        "unused-parameter",             -- Useful for documentation purposes.
        "unused-local-typedef",         -- We don't always use all typedefs.
        "missing-braces",               -- = {0} is OK.
        "microsoft-anon-tag",           -- Allow anonymous structs.
    }

filter {"platforms:Linux"}
    defines { "TM_OS_LINUX", "TM_OS_POSIX" }
    architecture "x64"
    defines {"TM_CPU_X64", "TM_CPU_AVX", "TM_CPU_SSE"}
    toolset "clang"
    includedirs { "$(TM_SDK_DIR)" }
    libdirs { "$(TM_SDK_DIR)/bin/Debug" }
    buildoptions {
        "-fms-extensions",                   -- Allow anonymous struct as C inheritance.
        "-g",                                -- Debugging.
        "-mavx",                             -- AVX.
        "-mfma",                             -- FMA.
        "-fcommon",                          -- Allow tentative definitions
    }
    enablewarnings {
        "shadow",
        "padded"
    }
    disablewarnings {
        "missing-field-initializers",   -- = {0} is OK.
        "unused-parameter",             -- Useful for documentation purposes.
        "unused-local-typedef",         -- We don't always use all typedefs.
        "missing-braces",               -- = {0} is OK.
        "microsoft-anon-tag",           -- Allow anonymous structs.
    }

filter "configurations:Debug"
    defines { "TM_CONFIGURATION_DEBUG", "DEBUG" }
    symbols "On"

filter "configurations:Release"
    defines { "TM_CONFIGURATION_RELEASE" }
    optimize "On"

main_dll {name = "tm-playground", dir = "src"}
main_exe({name = "tm-playground", dir = "src"}, true)

