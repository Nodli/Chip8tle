pathGRDK = "C:/Program Files (x86)/Microsoft GDK/221001/GRDK/"

workspace "VisualSolution"

    -- Configuration ; Platforms

    configurations { "Debug", "Release" }
    platforms { "x64" }

    filter "platforms:x64"
        toolset "msc"

    filter{}

    -- C Runtime Library

    staticruntime "on"
    characterset("ASCII")

    filter "toolset:msc"
        defines {"_CRT_SECURE_NO_WARNINGS"}

	filter {}

    -- filter "configurations:Debug"
    --    runtime "Debug"
    -- filter "configurations:Release"
       runtime "Release"

    filter {}

    -- Visual Studio Solution

    debugdir "bin/"
    objdir "tmp/obj"

    filter "system:windows"
        location "VisualStudio"
    filter "kind:WindowedApp"
        targetdir "bin/"
    filter "kind:StaticLib"
        targetdir "bin/lib"

    filter {}

    -- Build options ; Link options

    filter "toolset:msc"
        buildoptions { "/std:c++17 /nologo /EHs-c- /GR-" }
        linkoptions { "/NOLOGO" }

    filter "configurations:Debug"
        buildoptions{ "/fsanitize=address /Zi" }
        linkoptions{ "/INCREMENTAL:NO" }

    filter { "toolset:msc", "configurations:Debug" }
        buildoptions { "/Od" }
    filter { "toolset:msc", "configurations:Release" }
        buildoptions { "/Oi /O2" }

    filter {}

    -- Projects

    project "RAM"
        kind "WindowedApp"
        language "C++"

        files { "source/*.cpp", "source/*.h", "source/*.inl" }
        links { "dwmapi" }

        includedirs { "external" }

    filter {}
