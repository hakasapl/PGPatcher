[b][size=6][color=#9933ff]Overview[/color][/size][/b]

For a long time, there were primarily only parallax mods (or complex parallax but that's the same thing. See my rant on the naming of these things [url=https://github.com/hakasapl/PGPatcher/wiki/Supported-Shaders]here[/url]). The process for using them in-game was generally to install a ton of pre-patched parallax meshes (such as those offered by mods like Lux among others), and then installing [url=https://www.nexusmods.com/skyrimspecialedition/mods/79473]auto parallax[/url] to automatically disable parallax on meshes for which textures did not exist. This method of doing things is [b]very suboptimal and not clean[/b] for several reasons.

[list]
[*]Your coverage of parallax is limited to the meshes you have
[*]You need to use extra CPU time at runtime to disable some shaders that should have been disabled to begin with
[*]You are limited to the parallax shader type, whereas complex material has been available for some time and is a superior shader
[*]Alternate textures in plugins were often left not covered in this system
[*]Mod authors had to ship meshes with their textures, which often did not include fixes from mesh fix compilation mods like [url=https://www.nexusmods.com/skyrimspecialedition/mods/32117]assorted mesh fixes[/url]
[/list]
This led me to develop PGPatcher to solve all of these cases. PGPatcher is a dynamic patcher primarily responsible for managing how meshes in your load order are configured for various materials in Skyrim. In basic terms, as a user you can install whatever retexture mods you want (whether that be vanilla, parallax, complex material, or PBR), and then let PGPatcher patch your meshes and plugins to make it all work in-game. You no longer should install prepatched meshes nor [url=https://www.nexusmods.com/skyrimspecialedition/mods/79473]auto parallax[/url] as PGPatcher will take care of that for you. In addition, mixing and matching different shader types is supported. PGPatcher is capable of applying different shaders to the same mesh, and also handle alternate texture records which might need different shaders/properties through a comprehensive mesh permutation system. On top of these things, PGPatcher provides some optional convenience patchers, such as an automated [url=https://www.nexusmods.com/skyrimspecialedition/mods/53653]fixed mesh lighting[/url] patcher, autoconversion from parallax to complex material, and a lot more. See the [url=https://github.com/hakasapl/PGPatcher/wiki/Patchers]patchers wiki page[/url] for a full list.

From a texture mod author perspective, PGPatcher allows you to ship ONLY textures with your mod and nothing else (including plugins for alternate textures. PGPatcher will patch that for you too). You don't have to deal with prepatching any meshes, and the user can use your textures on their own meshes. The central idea behind PGPatcher is that textures should be separate from meshes and you shouldn't have to care about both at once. Should be a win-win all around. More information for mod authors can be found [url=https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors]here[/url].

As a user I understand the reluctance to learn and use yet another external tool. I always try to make things as easy and clear as possible and UX is a primary focus of mine for this reason. I hope the [url=https://github.com/hakasapl/PGPatcher/wiki]wiki[/url] is helpful for you. That being said I always look for feedback either here on nexus posts or in the form of a [url=https://github.com/hakasapl/PGPatcher/issues]GitHub issue[/url] if you feel there is something that could be clearer. You can also find me on the [url=https://discord.gg/3qef2Kf8uS]Parallax Den[/url] discord.

[size=4][b][color=#6aa84f][url=https://github.com/hakasapl/ParallaxGen/wiki/Basic-Usage]Getting Started and User Guide[/url][/color][/b][/size]
[size=4][b][color=#6aa84f][url=https://github.com/hakasapl/ParallaxGen/wiki/Troubleshooting-Guide]Troubleshooting Guide[/url][/color][/b][/size]

[size=6][color=#9933ff][b]Contributors[/b][/color][/size]

This is an open-source project. We would love to have others contribute as well; check out the project's [url=https://github.com/hakasapl/ParallaxGen]GitHub page[/url] for more information. The following authors have contributed to PG:

[list]
[*][url=https://next.nexusmods.com/profile/hakasapl/about-me]hakasapl[/url]
[*][url=https://next.nexusmods.com/profile/Sebastian1981/about-me]Sebastian1981[/url]
[*][url=https://next.nexusmods.com/profile/RealExist/about-me]RealExist[/url]
[/list]
A special thanks to:

[list]
[*]The amazing folks on the community shaders discord who answer all my ridiculous technical questions.
[*][url=https://www.nexusmods.com/skyrimspecialedition/users/1366316]Spongeman131[/url] for their SSEedit script [url=https://www.nexusmods.com/skyrimspecialedition/mods/33846]NIF Batch Processing Script[/url], which was instrumental on figuring out how to modify meshes to enable parallax
[*][url=https://www.nexusmods.com/skyrimspecialedition/users/930789]Kulharin[/url] for their SSEedit script [url=https://www.nexusmods.com/skyrimspecialedition/mods/96777/?tab=files]itAddsComplexMaterialParallax[/url], which served the same purpose for figuring out how to modify meshes to enable complex material
[/list]
The following libraries made PGPatcher possible - send them your thanks!

[list]
[*][url=https://github.com/Mutagen-Modding/Mutagen]Mutagen[/url] for bethesda plugin interaction
[*][url=https://github.com/Ryan-rsm-McKenzie/bsa]rsm-bsa[/url] for reading BSA files
[*][url=https://github.com/ousnius/nifly]nifly[/url] for modifying NIF files
[/list]
[url=https://discord.gg/3qef2Kf8uS][img]https://raw.githubusercontent.com/hakasapl/PGPatcher/refs/heads/main/nexus/discord.png[/img][/url]

[url=https://github.com/hakasapl/PGPatcher][img]https://img.shields.io/github/stars/hakasapl/pgpatcher?style=flat&logo=github&label=hakasapl/PGPatcher[/img][/url]

[url=https://ko-fi.com/hakasapl][img]https://github.com/hakasapl/PGPatcher/blob/main/nexus/kofi.png?raw=true[/img][/url]
