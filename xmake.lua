set_project("pacman")
set_version("1.0.0")
set_xmakever("2.9.0")

add_repositories("local-repo xmake/repo")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})
set_defaultmode("release")
set_allowedmodes("debug", "release", "releasedbg")

option("native")
    set_default(true)
    set_showmenu(true)
    set_description("Tune code generation for the build machine (-march=native: AVX-512, BMI2, FMA, ...)")
option_end()

set_languages("c17")
set_warnings("allextra")
add_cflags("-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wvla", "-Wdouble-promotion")

if has_config("native") then
    add_cflags("-march=native", "-mtune=native")
end

if is_mode("release", "releasedbg") then
    set_optimize("fastest")
    set_policy("build.optimization.lto", true)
    add_cflags("-fno-plt", "-fno-semantic-interposition", "-fno-math-errno")
    add_defines("NDEBUG")
end
if is_mode("release") then
    set_strip("all")
end
if is_mode("debug") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
end

-- raylib is compiled from source by xmake with the same CPU tuning and LTO, so the
-- final link optimizes across the engine and the game.
add_requires("raylib-native 5.5", {configs = {
    lto = is_mode("release", "releasedbg"),
    cflags = has_config("native") and "-march=native -mtune=native" or nil
}})

-- Platform-independent helpers: clocks, statistics.
target("core")
    set_kind("static")
    add_files("src/core/*.c")
    add_includedirs("src", {public = true})
    add_syslinks("m", {public = true})

-- Arcade simulation. Pure integer logic, no rendering or OS dependencies.
target("game")
    set_kind("static")
    add_deps("core")
    add_files("src/game/*.c")

-- Game state -> sprite quads. CPU only, no graphics API.
target("scene")
    set_kind("static")
    add_deps("game")
    add_files("src/scene/*.c")

-- OpenGL 4.5 pipeline and atlas construction (raylib decodes the PNGs, owns the context).
target("gpu")
    set_kind("static")
    add_deps("scene")
    add_files("src/gpu/*.c")
    add_packages("raylib-native", {public = true})

-- Audio synthesizer: pure C mixer, no device or OS dependencies.
target("synth")
    set_kind("static")
    add_deps("core")
    add_files("src/audio/synth.c")

-- Audio device glue (raylib/miniaudio stream + lock-free command queue).
target("audio")
    set_kind("static")
    add_deps("synth")
    add_files("src/audio/audio_device.c")
    add_packages("raylib-native", {public = true})

target("pacman")
    set_kind("binary")
    add_deps("gpu", "audio")
    add_files("src/app/*.c")
    after_build(function (target)
        os.cp(path.join(os.projectdir(), "assets"), target:targetdir())
    end)

-- Headless CPU benchmarks: simulation, autopilot, scene, maze art, synth. No graphics/audio device.
target("bench_cpu")
    set_kind("binary")
    set_default(false)
    add_deps("scene", "synth")
    add_files("bench/*.c")
    add_includedirs(".")

task("bench")
    set_category("action")
    on_run(function ()
        import("core.base.option")
        local seconds = option.get("seconds")
        os.execv(os.programfile(), {"build", "pacman"})
        os.execv(os.programfile(), {"build", "bench_cpu"})
        os.execv(os.programfile(), {"run", "bench_cpu"})
        os.execv(os.programfile(), {"run", "pacman", "--bench", seconds})
    end)
    set_menu {
        usage = "xmake bench [options]",
        description = "Build and run the CPU benchmarks and the full render benchmark.",
        options = {
            {"s", "seconds", "kv", "10", "Duration of the render benchmark in seconds."}
        }
    }
