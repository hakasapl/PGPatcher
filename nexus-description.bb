[center][b]PG is in BETA. It has been successfully used by many but is still in development. Please be patient and file detailed bug reports if you encounter any.[/b][/center]
[center]The GitHub page for this open-source project can be found [url=https://github.com/hakasapl/ParallaxGen]here[/url]. If you have time to give a star on GitHub I would appreciate it![/center]

[size=6][center][b][color=#6aa84f][url=https://github.com/hakasapl/ParallaxGen/wiki/Basic-Usage]Getting Started / Requirements[/url][/color][/b][/center][/size]
[size=6][center][b][color=#6aa84f][url=https://github.com/hakasapl/ParallaxGen/wiki/Troubleshooting-Guide]Troubleshooting Guide[/url][/color][/b][/center][/size]
[b][size=6][center][color=#6aa84f]Features[/color][/center][/size][/b]
[size=4]Mesh Patching[/size]

This is the primary goal of PG. Despite the name, PG can be used for more than just patching meshes for Parallax. Without any configuration, PG will patch any mesh in your load order (including meshes in your BSA archives) for [color=#bf9000][b]parallax, complex material, and/or truepbr[/b][/color]. What shapes being patched for which shader is dependent on the textures you have in your load order. Each of those patchers can be disabled from within the GUI.

Before this, mod authors would need to ship pre-patched meshes with their texture mods. The problem with this is that the mesh can be overwritten, or you may want to use a different mesh with this texture, for example a fixed mesh from one of the many mesh fix mods. The goal of this feature is to completely remove this consideration. The winning mesh is the mesh you want, purely based on mesh things like UV fixes, split meshes, the model itself, etc. The winning texture is the one applied to the matching mesh after running PG.

In addition, sometimes there is a need to combine shaders in a single mesh. For example, your mountains might be complex material, and your whiterun street might be regular parallax, but a single whiterun street mesh has some rocks on the side of the road. Before, this would have to be done manually using applications like NifSkope. PG will know which should be which and patch the mesh automatically for both.

Want to learn more about all the shader types PG supports? Check out this [url=https://github.com/hakasapl/ParallaxGen/wiki/Supported-Shaders]wiki page[/url].

[size=4]Plugin Patching[/size]

Plugins (mainly TXST records) will also be patched according to how the original shapes were patched during mesh patching. A resulting ParallaxGen.esp in the output. PG will also automatically duplicate meshes when mixing shader types if a given alternate texture record cannot be applied to the originally patched shader.

[size=4]Shader Transforms[/size]

Complex parallax has a lot of limitations that I didn't like, so I implemented shader upgrades into PG. What this means is that by chosing "Upgrade Parallax to Complex Material", PG will generate complex material environment mask files for every parallax height map you have in your load order, if an existing complex material map does not already exist. Then, when patching meshes, PG will use those newly generated textures and enable complex material on meshes instead of complex parallax. In most cases you won't see any difference in-game between complex parallax and complex material since PG does not add any extra data such as metalness and roughness (so you should still prefer to get complex material mods, which will). However, this will allow you to use single-pass snow shaders (ie. Vanilla or Better Dynamic Snow v3), see parallax on decals, and many more things that complex material supports where complex parallax doesn't. [b]In most cases I recommend enabling this option always[/b]. This also fixes the dreaded blue/white distant mesh bug with parallax meshes. After running with this option the result will be NO complex parallax in your game.

[size=4]Other Things![/size]

Such as [url=https://github.com/hakasapl/ParallaxGen/wiki/PGTools]pgtools modding tools[/url], limited texture patching, and a bit more.

[color=#6aa84f][size=6][center][b]A Note to Mod Authors[/b][/center][/size][/color]
One of the main goals for PG is to allow texture artists to worry about just that, and not the mesh part. Historically parallax or complex material meshes needed to be included in mods for them to work in-game. With PG you should be able to ship only textures with your mod, and send your users to here to patch their meshes. That being said, the way PG identifies textures is not fool-proof. See this [url=https://github.com/hakasapl/ParallaxGen/wiki/Mod-Authors]wiki page[/url] for more information for mod authors.

[size=6][center][color=#6aa84f][b]Contributors[/b][/color][/center][/size]This is an open-source project. We would love to have others contribute as well; check out the project's [url=https://github.com/hakasapl/ParallaxGen]GitHub page[/url] for more information. The following authors have contributed to PG:

[list]
[*][url=https://next.nexusmods.com/profile/hakasapl/about-me]hakasapl[/url]
[*][url=https://next.nexusmods.com/profile/Sebastian1981/about-me]Sebastian1981[/url]
[*][url=https://next.nexusmods.com/profile/RealExist/about-me]RealExist[/url]
[/list]
[size=6][b][center][color=#6aa84f]Acknowledgements[/color][/center][/b][/size]
A special thanks to:

[list]
[*]In no particular order the amazing folks on the community shaders discord who answer all my questions (not limited to this list): [url=https://next.nexusmods.com/profile/leostevano/about-me]Leostevano[/url], [url=https://next.nexusmods.com/profile/Faultierfan/about-me]Faultier[/url], [url=https://next.nexusmods.com/profile/RealExist/about-me]RealExist[/url]
[*][url=https://www.nexusmods.com/skyrimspecialedition/users/1366316]Spongeman131[/url] for their SSEedit script [url=https://www.nexusmods.com/skyrimspecialedition/mods/33846]NIF Batch Processing Script[/url], which was instrumental on figuring out how to modify meshes to enable parallax
[*][url=https://www.nexusmods.com/skyrimspecialedition/users/930789]Kulharin[/url] for their SSEedit script [url=https://www.nexusmods.com/skyrimspecialedition/mods/96777/?tab=files]itAddsComplexMaterialParallax[/url], which served the same purpose for figuring out how to modify meshes to enable complex material
[/list]

A ton of libraries were used in this project - send them a thanks!

[list]
[*][url=https://github.com/Mutagen-Modding/Mutagen]Mutagen[/url] for bethesda plugin interaction
[*][url=https://github.com/Ryan-rsm-McKenzie/bsa]rsm-bsa[/url] for reading BSA files
[*][url=https://github.com/ousnius/nifly]nifly[/url] for modifying NIF files
[/list]
