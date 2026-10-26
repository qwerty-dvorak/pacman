package("raylib-native")
    set_homepage("https://www.raylib.com")
    set_description("raylib built from source with xmake: desktop GLFW/X11 backend, stripped feature set, custom frame control.")
    set_license("zlib")

    add_urls("https://github.com/raysan5/raylib/archive/refs/tags/$(version).tar.gz",
             "https://github.com/raysan5/raylib.git")
    add_versions("5.5", "aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e")

    -- GLFW 3.4 dlopen()s libX11/libGL at runtime, so only the C runtime is linked.
    add_syslinks("pthread", "dl", "m")

    on_install("linux", function (package)
        local config = "src/config.h"

        -- The application owns the frame: swap, event polling and pacing are explicit calls.
        io.replace(config, "//#define SUPPORT_CUSTOM_FRAME_CONTROL    1", "#define SUPPORT_CUSTOM_FRAME_CONTROL    1", {plain = true})

        -- Strip subsystems the game never touches. Gestures, automation and screen capture
        -- otherwise run work on every frame; the others only cost binary size and init time.
        -- rtext stays compiled because rcore calls its string helpers, but the default font
        -- (a texture upload at InitWindow) and font file loaders are off. raudio stays for its
        -- miniaudio device and AudioStream callback; every sound is synthesized, so no audio
        -- file decoder is compiled.
        local disabled = {
            "SUPPORT_MODULE_RSHAPES", "SUPPORT_MODULE_RMODELS",
            "SUPPORT_CAMERA_SYSTEM", "SUPPORT_GESTURES_SYSTEM", "SUPPORT_MOUSE_GESTURES", "SUPPORT_SSH_KEYBOARD_RPI",
            "SUPPORT_SCREEN_CAPTURE", "SUPPORT_GIF_RECORDING", "SUPPORT_COMPRESSION_API", "SUPPORT_AUTOMATION_EVENTS",
            "SUPPORT_FILEFORMAT_GIF", "SUPPORT_FILEFORMAT_QOI", "SUPPORT_FILEFORMAT_DDS", "SUPPORT_IMAGE_EXPORT",
            "SUPPORT_IMAGE_GENERATION", "SUPPORT_CLIPBOARD_IMAGE", "SUPPORT_DEFAULT_FONT", "SUPPORT_FILEFORMAT_TTF",
            "SUPPORT_FILEFORMAT_FNT", "SUPPORT_FONT_ATLAS_WHITE_REC",
            "SUPPORT_FILEFORMAT_WAV", "SUPPORT_FILEFORMAT_OGG", "SUPPORT_FILEFORMAT_MP3", "SUPPORT_FILEFORMAT_QOA",
            "SUPPORT_FILEFORMAT_FLAC", "SUPPORT_FILEFORMAT_XM", "SUPPORT_FILEFORMAT_MOD"
        }
        for _, flag in ipairs(disabled) do
            io.gsub(config, "\n#define " .. flag .. "%s", "\n//#define " .. flag .. " ")
        end

        io.writefile("xmake.lua", [[
add_rules("mode.debug", "mode.release")
target("raylib")
    set_kind("static")
    set_languages("gnu99")
    set_warnings("none")
    add_files("src/rcore.c", "src/rtextures.c", "src/rtext.c", "src/utils.c", "src/rglfw.c", "src/raudio.c")
    add_includedirs("src", "src/external/glfw/include")
    add_headerfiles("src/raylib.h")
    add_defines("PLATFORM_DESKTOP_GLFW", "GRAPHICS_API_OPENGL_33", "_GLFW_X11", "_GNU_SOURCE")
    if is_mode("release") then
        set_optimize("fastest")
    end
]])
        local configs = {mode = package:is_debug() and "debug" or "release"}
        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("InitWindow", {includes = "raylib.h"}))
        assert(package:has_cfuncs("LoadImage", {includes = "raylib.h"}))
        assert(package:has_cfuncs("SetAudioStreamCallback", {includes = "raylib.h"}))
    end)
