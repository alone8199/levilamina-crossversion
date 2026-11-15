-- CrossVersion: 单插件 / 按 BDS 版本分别构建
-- 每次构建只产出一个 CrossVersion.dll，由 --ll_version / --cv_server_protocol 决定目标版本。
-- 按版本差异（hook 符号表）由 scripts/generate_matrix.py 生成的 src/generated/TargetConfig.h 注入。

add_rules("mode.debug", "mode.release")
add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

option("ll_version")
    set_default("26.60.4")
    set_showmenu(true)
    set_description("LeviLamina package version for the target BDS")
option_end()

option("cv_server_protocol")
    set_default("2225")
    set_showmenu(true)
    set_description("Network protocol version of the target BDS")
option_end()

option("cv_bds_version")
    set_default("1.26.60")
    set_showmenu(true)
    set_description("Target BDS version string (baked into the DLL)")
option_end()

option("cv_codec_dir")
    set_default("codecs")
    set_showmenu(true)
    set_description("Directory (relative to plugin dir) containing codec_v*.dll")
option_end()

local llVersion = get_config("ll_version")

add_requires("levilamina " .. llVersion, {configs = {target_type = "server"}})
add_requires("nlohmann_json")

target("CrossVersion")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")

    add_packages("levilamina", "nlohmann_json")
    add_includedirs("src")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")

    add_defines(
        "NOMINMAX",
        "UNICODE",
        "_CRT_SECURE_NO_WARNINGS",
        "LL_PLAT_S",
        "CV_SERVER_PROTOCOL=" .. get_config("cv_server_protocol"),
        "CV_BDS_VERSION=\"" .. get_config("cv_bds_version") .. "\"",
        "CV_CODEC_DIR=\"" .. get_config("cv_codec_dir") .. "\""
    )

    if is_plat("windows") then
        add_cxflags("/utf-8", "/W4", "/EHa")
        add_defines("_HAS_CXX23=1")
    end

    after_build(function (target)
        local out = path.join(os.projectdir(), "bin", "CrossVersion")
        os.mkdir(path.join(out, "lang"))
        os.mkdir(path.join(out, "codecs"))
        os.cp(target:targetfile(), path.join(out, "CrossVersion.dll"))
        os.cp(path.join(os.projectdir(), "manifest.json"), out)
        os.cp(path.join(os.projectdir(), "config", "config.jsonc"), out)
        os.cp(path.join(os.projectdir(), "lang", "*"), path.join(out, "lang"))
        cprint("${green}[CrossVersion] staged -> %s${clear}", out)
    end)
