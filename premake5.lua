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

-- Returns a snake-case version of the kebab-case string `name`.
function snake_case(name)
    return string.gsub(name, "-", "_")
end

-- Include all project files from specified folder
function folder(t)
    if type(t) ~= "table" then t = {t} end
    for _,f in ipairs(t) do
        files {f .. "/**.h",  f .. "/**.c", f .. "/**.inl", f .. "/**.cpp", f .. "/**.m", f .. "/**.metal"}
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
    filter 'files:**.metal'
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

        local dir = "samples/" .. sn
        folder {dir}
        removefiles {dir .. "/host.c"}
        links {"Foundation.framework", "QuartzCore.framework", "Metal.framework"}
        cppdialect "C++17"
        removeplatforms { "x64", "Linux" }
        includedirs { "", lib_path("metal-cpp-2020-11-macosx/include") }
        copy_shaders_to_data_dir()
end

function main_exe(t, copy_data)
    if type(t) == "string" then t = {name=t} end
    local sn = snake_case(t.name)
    base(t.name .. "-exe")
        location("build/" .. sn .. "_exe")
        targetname(t.name)
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
        language "C++"
        targetdir "bin/%{cfg.buildcfg}"
        defines { "TM_LINKS_FOUNDATION" }
        dependson {"foundation", t.name .. "-dll"}
        links {"foundation"}
        local dir = "samples/" .. sn
        files {dir .. "/host.c"}

        -- Only true for the-machinery.exe since multiple projects doing this with a multi-threaded build
        -- leads to access violation race conditions.
        if copy_data then
            postbuildcommands {
                '{MKDIR} ../../bin/%{cfg.buildcfg}/plugins', 
                '{MKDIR} ../../bin/%{cfg.buildcfg}/data/shaders', 
            }
            filter { "platforms:Linux or MacOSX-x64 or MacOSX-ARM" }
                postbuildcommands {
                    '{COPY} "${TM_SDK_DIR}/bin/Debug/plugins/libtm_os_window.dylib" ../../bin/%{cfg.buildcfg}/plugins',
                }
            filter {}
        end
end


workspace "metal-samples"
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

filter { "system:macosx" }
    platforms { "MacOSX-x64", "MacOSX-ARM" }

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
    includedirs { "$(TM_SDK_DIR)" }
    libdirs { "$(TM_SDK_DIR)/bin/Debug" }
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

filter "configurations:Debug"
    defines { "TM_CONFIGURATION_DEBUG", "DEBUG" }
    symbols "On"

filter "configurations:Release"
    defines { "TM_CONFIGURATION_RELEASE" }
    optimize "On"

main_dll {name = "performing-calculations-on-gpu"}
main_exe({name = "performing-calculations-on-gpu"}, true)

