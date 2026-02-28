<p align="center"><img width="200" alt="image" src="https://raw.githubusercontent.com/hakasapl/PGPatcher/refs/heads/main/nexus/icon-highres.png" /></p>

<h1 align="center">PGPatcher - Mesh and Texture Patcher for Skyrim</h1>

For a long time, there were primarily only parallax mods (or complex parallax but that's the same thing. See my rant on the naming of these things [here](https://github.com/hakasapl/PGPatcher/wiki/Supported-Shaders)). The process for using them in-game was generally to install a ton of pre-patched parallax meshes (such as those offered by mods like Lux among others), and then installing [auto parallax](https://www.nexusmods.com/skyrimspecialedition/mods/79473) to automatically disable parallax on meshes for which textures did not exist. This method of doing things is **very suboptimal and not clean** for several reasons.

- Your coverage of parallax is limited to the meshes you have
- You need to use extra CPU time at runtime to disable some shaders that should have been disabled to begin with
- You are limited to the parallax shader type, whereas complex material has been available for some time and is a superior shader
- Alternate textures in plugins were often left not covered in this system
- Mod authors had to ship meshes with their textures, which often did not include fixes from mesh fix compilation mods like [assorted mesh fixes](https://www.nexusmods.com/skyrimspecialedition/mods/32117)

This led me to develop PGPatcher to solve all of these cases. PGPatcher is a dynamic patcher primarily responsible for managing how meshes in your load order are configured for various materials in Skyrim. In basic terms, as a user you can install whatever retexture mods you want (whether that be vanilla, parallax, complex material, or PBR), and then let PGPatcher patch your meshes and plugins to make it all work in-game. You no longer should install prepatched meshes nor [auto parallax](https://www.nexusmods.com/skyrimspecialedition/mods/79473) as PGPatcher will take care of that for you. In addition, mixing and matching different shader types is supported. PGPatcher is capable of applying different shaders to the same mesh, and also handle alternate texture records which might need different shaders/properties through a comprehensive mesh permutation system. On top of these things, PGPatcher provides some optional convenience patchers, such as an automated [fixed mesh lighting](https://www.nexusmods.com/skyrimspecialedition/mods/53653) patcher, autoconversion from parallax to complex material, and a lot more. See the [patchers wiki page](https://github.com/hakasapl/PGPatcher/wiki/Patchers) for a full list.

From a texture mod author perspective, PGPatcher allows you to ship ONLY textures with your mod and nothing else (including plugins for alternate textures. PGPatcher will patch that for you too). You don't have to deal with prepatching any meshes, and the user can use your textures on their own meshes. The central idea behind PGPatcher is that textures should be separate from meshes and you shouldn't have to care about both at once. Should be a win-win all around. More information for mod authors can be found [here](https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors).

As a user I understand the reluctance to learn and use yet another external tool. I always try to make things as easy and clear as possible and UX is a primary focus of mine for this reason. I hope the [wiki](https://github.com/hakasapl/PGPatcher/wiki) is helpful for you. That being said I always look for feedback either here on nexus posts or in the form of a [GitHub issue](https://github.com/hakasapl/PGPatcher/issues) if you feel there is something that could be clearer. You can also find me on the [Parallax Den](https://discord.gg/3qef2Kf8uS) discord.

## Resources and Documentation

* [Getting Started and User Guide](https://github.com/hakasapl/PGPatcher/wiki/Basic-Usage)
* [FAQ](https://github.com/hakasapl/PGPatcher/wiki/FAQ)
* [Troubleshooting Guide](https://github.com/hakasapl/PGPatcher/wiki/Troubleshooting-Guide)

[DarkFox127](https://www.nexusmods.com/profile/DarkFox127) made a fantastic [tutorial video](https://www.youtube.com/watch?v=-ZbQBQ05_Ss) for those who prefer a video for getting started

## Contributors

This is an open-source project. We would love to have others contribute as well! See [CONTRIBUTING.md](https://github.com/hakasapl/PGPatcher/blob/main/CONTRIBUTING.md) for more details about the project.

## Acknowledgments

* The amazing folks on the community shaders discord who answer all my ridiculous technical questions.
* [Spongeman131](https://www.nexusmods.com/skyrimspecialedition/users/1366316) for their SSEedit script [NIF Batch Processing Script](https://www.nexusmods.com/skyrimspecialedition/mods/33846), which was instrumental on figuring out how to modify meshes to enable parallax
* [Kulharin](https://www.nexusmods.com/skyrimspecialedition/users/930789) for their SSEedit script [itAddsComplexMaterialParallax](https://www.nexusmods.com/skyrimspecialedition/mods/96777/?tab=files), which served the same purpose for figuring out how to modify meshes to enable complex material

The following libraries made PGPatcher possible - send them your thanks!

* [ousnius/nifly](https://github.com/ousnius/nifly)
* [Mutagen-Modding/Mutagen](https://github.com/Mutagen-Modding/Mutagen)
* [Ryan-rsm-McKenzie/bsa](https://github.com/Ryan-rsm-McKenzie/bsa)
* [CLIUtils/cli11](https://github.com/CLIUtils/CLI11)
* [jeremy-rifkin/cpptrace](https://github.com/jeremy-rifkin/cpptrace)
* [microsoft/directxtex](https://github.com/microsoft/DirectXTex)
* [nlohmann/json](https://github.com/nlohmann/json)
* [richgel999/miniz](https://github.com/richgel999/miniz)
* [gabime/spdlog](https://github.com/gabime/spdlog)
* [wxWidgets/wxWidgets](https://github.com/wxWidgets/wxWidgets)
* [google/flatbuffers](https://github.com/google/flatbuffers)
* [boostorg/boost](https://github.com/boostorg/boost)
