namespace PGMutagen;

// Base Imports
using System;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

// Mutagen
using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Environments;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Noggog;

// Harmony
using HarmonyLib;
using Mutagen.Bethesda.Plugins.Exceptions;
using Mutagen.Bethesda.Strings;
using Mutagen.Bethesda.Plugins.Binary.Streams;
using Mutagen.Bethesda.Strings.DI;
using Mutagen.Bethesda.Plugins.Aspects;

using Google.FlatBuffers;
using System.Collections;

public static class ExceptionHandler
{
    private static string? LastExceptionMessage;

    public static void SetLastException(Exception? ex)
    {
        // Find the deepest inner exception
        while (ex is not null)
        {
            LastExceptionMessage += "\n" + ex.Message + "\n" + ex.StackTrace;
            ex = ex.InnerException;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetLastException", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetLastException([DNNE.C99Type("wchar_t**")] IntPtr* errorMessagePtr)
    {
        string? errorMessage = LastExceptionMessage ?? string.Empty;

        if (LastExceptionMessage.IsNullOrEmpty())
        {
            return;
        }

        *errorMessagePtr = Marshal.StringToHGlobalUni(errorMessage);
    }
}

public static class MessageHandler
{
    private static Queue<Tuple<string, int>> LogQueue = [];

    public static void Log(string message, int level = 0)
    {
        // Level values
        // 0: Trace
        // 1: Debug
        // 2: Info
        // 3: Warning
        // 4: Error
        // 5: Critical
        LogQueue.Enqueue(new Tuple<string, int>(message, level));
    }

    [UnmanagedCallersOnly(EntryPoint = "GetLogMessage", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetLogMessage([DNNE.C99Type("wchar_t**")] IntPtr* messagePtr, [DNNE.C99Type("int*")] int* level)
    {
        if (LogQueue.Count == 0)
        {
            return;
        }

        var logMessage = LogQueue.Dequeue();
        string message = logMessage.Item1;
        *messagePtr = Marshal.StringToHGlobalUni(logMessage.Item1);
        *level = logMessage.Item2;
    }
}

class StructuralArrayComparer : IEqualityComparer<string[]>
{
    public bool Equals(string[]? x, string[]? y)
    {
        return StructuralComparisons.StructuralEqualityComparer.Equals(x, y);
    }

    public int GetHashCode(string[] obj)
    {
        return StructuralComparisons.StructuralEqualityComparer.GetHashCode(obj);
    }
}

public class PGMutagen
{
    //
    // Class Members
    //

    private static SkyrimMod? OutMod;
    private static IGameEnvironment<ISkyrimMod, ISkyrimModGetter>? Env;
    private static Dictionary<string, List<Tuple<FormKey, string>>> ModelUses = [];
    private static HashSet<IModelGetter> ProcessedModelUses = [];
    private static Dictionary<FormKey, IMajorRecord> ModifiedRecords = [];
    private static Dictionary<string[], Tuple<ITextureSet, bool>> NewTextureSets = new(new StructuralArrayComparer());
    private static SortedSet<uint> allocatedFormIDs = [];
    private static HashSet<FormKey> formKeyErrorsPosted = [];
    private static uint lastUsedFormID = 1;


    private static SkyrimRelease GameType;
    private static Language PluginLanguage = Language.English;

    // tracks masters in each split plugin
    private static List<HashSet<ModKey>> OutputMasterTracker = [];
    private static List<SkyrimMod> OutputSplitMods = [];

    private class Utf8EncodingWrapper : IMutagenEncodingProvider
    {
        public IMutagenEncoding GetEncoding(GameRelease release, Language language)
        {
            return MutagenEncoding._utf8;
        }
    }

    //
    // Public API
    //

    [UnmanagedCallersOnly(EntryPoint = "Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void Initialize(
      [DNNE.C99Type("const int")] int gameType,
      [DNNE.C99Type("const wchar_t*")] IntPtr exePath,
      [DNNE.C99Type("const wchar_t*")] IntPtr dataPathPtr,
      [DNNE.C99Type("const wchar_t**")] IntPtr* loadOrder,
      [DNNE.C99Type("const unsigned int")] uint pluginLang)
    {
        try
        {
            // Setup harmony patches
            string ExePath = Marshal.PtrToStringUni(exePath) ?? string.Empty;
            PatchBaseDirectory.BaseDirectory = ExePath;

            var harmony = new Harmony("com.github.hakasapl.pgpatcher.pgmutagen");
            harmony.PatchAll();

            // Main method
            string dataPath = Marshal.PtrToStringUni(dataPathPtr) ?? string.Empty;
            MessageHandler.Log("[Initialize] Data Path: " + dataPath, 0);

            PluginLanguage = (Language)pluginLang;
            GameType = (SkyrimRelease)gameType;
            OutMod = new SkyrimMod(ModKey.FromFileName("PGPatcher.esp"), GameType);

            List<ModKey> loadOrderList = [];
            for (int i = 0; loadOrder[i] != IntPtr.Zero; i++)
            {
                loadOrderList.Add(Marshal.PtrToStringUni(loadOrder[i]) ?? string.Empty);
            }

            var stringReadParams = new StringsReadParameters()
            {
                TargetLanguage = PluginLanguage,
                EncodingProvider = new Utf8EncodingWrapper()
            };

            try
            {
                Env = GameEnvironment.Typical.Builder<ISkyrimMod, ISkyrimModGetter>((GameRelease)GameType)
                    .WithTargetDataFolder(dataPath)
                    .WithLoadOrder(loadOrderList.ToArray())
                    .WithOutputMod(OutMod)
                    .WithStringParameters(stringReadParams)
                    .Build();
            }
            catch (Exception ex)
            {
                MessageHandler.Log("Failed to build plugin environment. This is usually due to a bad plugin in your load order. Message: " + ex.Message, 5);
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "PopulateObjs", CallConvs = [typeof(CallConvCdecl)])]
    public static void PopulateObjs([DNNE.C99Type("const wchar_t*")] IntPtr oldPGPluginPath)
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before PopulateObjs");
            }

            if (OutMod is null)
            {
                throw new Exception("OutMod is null in PopulateObjs");
            }

            // Get path to old PG plugin
            string oldModPath = Marshal.PtrToStringUni(oldPGPluginPath) ?? string.Empty;
            if (!oldModPath.IsNullOrEmpty() && File.Exists(oldModPath))
            {
                MessageHandler.Log("Loading old output plugin for cache " + oldModPath, 2);
                // Add all old PG records to output mod
                using var oldMod = SkyrimMod.Create(GameType).FromPath(oldModPath).Construct();
                // loop through all texture sets in old mod
                foreach (var txst in oldMod.TextureSets)
                {
                    var newFormKey = new FormKey(OutMod.ModKey, txst.FormKey.ID);
                    var newTXSTObj = txst.Duplicate(newFormKey);

                    // Add to output mod
                    OutMod.TextureSets.Add(newTXSTObj);
                    allocatedFormIDs.Add(newTXSTObj.FormKey.ID);
                    lastUsedFormID = newTXSTObj.FormKey.ID;

                    // Add to dictionary (mark as unused)
                    var textures = GetTextureSet(newTXSTObj);
                    NewTextureSets[textures] = new Tuple<ITextureSet, bool>(newTXSTObj, false);
                }
            }

            foreach (var modelMajorRec in EnumerateModelRecordsSafe())
            {
                // Will store models to check later
                var ModelRecs = GetModelElems(modelMajorRec);

                if (ModelRecs.Count == 0)
                {
                    // Skip if there are no MODL records in this record
                    continue;
                }

                foreach (var modelRec in ModelRecs)
                {
                    // add to model uses
                    string meshName;
                    try
                    {
                        if (modelRec.Item1.File.IsNullOrEmpty())
                        {
                            continue;
                        }
                        meshName = modelRec.Item1.File.ToString().ToLower();
                    }
                    catch (Exception)
                    {
                        if (formKeyErrorsPosted.Add(modelMajorRec.FormKey))
                        {
                            MessageHandler.Log("Unable to read model path. This should be reported to the plugin author: " + GetRecordDesc(modelMajorRec), 4);
                        }
                        continue;
                    }

                    if (!ModelUses.ContainsKey(meshName))
                    {
                        ModelUses[meshName] = [];
                    }

                    var curTuple = new Tuple<FormKey, string>(modelMajorRec.FormKey, modelRec.Item2);
                    ModelUses[meshName].Add(curTuple);

                    // Check if we need to also add the weight counterpart
                    if (modelMajorRec is IArmorAddonGetter || modelMajorRec is IArmorGetter)
                    {
                        string weightMeshName = string.Empty;
                        if (meshName.EndsWith("_0.nif"))
                        {
                            // replace _0.nif with _1.nif
                            weightMeshName = string.Concat(meshName.AsSpan(0, meshName.Length - 6), "_1.nif");
                        }
                        else if (meshName.EndsWith("_1.nif"))
                        {
                            // replace _1.nif with _0.nif
                            weightMeshName = string.Concat(meshName.AsSpan(0, meshName.Length - 6), "_0.nif");
                        }

                        if (!weightMeshName.IsNullOrEmpty())
                        {
                            if (!ModelUses.ContainsKey(weightMeshName))
                            {
                                ModelUses[weightMeshName] = [];
                            }

                            ModelUses[weightMeshName].Add(curTuple);
                        }
                    }
                }
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "Finalize", CallConvs = [typeof(CallConvCdecl)])]
    public static void Finalize([DNNE.C99Type("const wchar_t*")] IntPtr outputPathPtr, [DNNE.C99Type("const int")] int esmify)
    {
        try
        {
            if (Env is null || OutMod == null)
            {
                throw new Exception("Initialize must be called before Finalize");
            }

            // Remove unused texture sets from NewTextureSets
            foreach (var texSetEntry in NewTextureSets)
            {
                if (!texSetEntry.Value.Item2)
                {
                    // not used, remove from OutMod
                    OutMod.TextureSets.Remove(texSetEntry.Value.Item1);
                }
            }

            // calculate maximum form ID
            uint maxFormID = 0;
            foreach (var txst in OutMod.TextureSets)
            {
                maxFormID = Math.Max(maxFormID, txst.FormKey.ID);
            }

            // Get output path from C++
            string outputPath = Marshal.PtrToStringUni(outputPathPtr) ?? string.Empty;

            // Check if OutMod can be ESL flagged
            if (maxFormID <= 0xFFF)
            {
                OutMod.IsSmallMaster = true;
            }
            OutMod.IsMaster = true;

            bool OutModNeeded = OutMod.EnumerateMajorRecords().Any();
            bool HasModifiedRecords = ModifiedRecords.Count > 0;

            if (!OutModNeeded && !HasModifiedRecords)
            {
                // No records to write, return early
                return;
            }

            if (OutModNeeded)
            {
                // Write OutMod
                // We disable formid compactness check for VR specifically as we require Skyrim VR ESL support mod so we CAN use those extra formids
                OutMod.BeginWrite
                    .ToPath(Path.Combine(outputPath, OutMod.ModKey.FileName))
                    .WithLoadOrder(Env.LoadOrder)
                    .NoFormIDCompactnessCheck()
                    .Write();
            }

            // Add all modified model records to the output mod
            foreach (var rec in ModifiedRecords)
            {
                var ModifiedRecord = rec.Value;

                var outputMod = GetModToAdd(ModifiedRecord);

                if (ModifiedRecord is ITranslatedNamed namedRec)
                {
                    if (namedRec.Name != null && namedRec.Name.TryLookup(PluginLanguage, out var localizedName))
                    {
                        namedRec.Name = localizedName;
                    }
                }

                if (ModifiedRecord is Mutagen.Bethesda.Skyrim.Activator activator)
                {
                    if (activator.ActivateTextOverride != null && activator.ActivateTextOverride.TryLookup(PluginLanguage, out var localizedActivateText))
                    {
                        activator.ActivateTextOverride = localizedActivateText;
                    }
                    outputMod.Activators.Add(activator);
                }
                else if (ModifiedRecord is Ammunition ammunition)
                {
                    outputMod.Ammunitions.Add(ammunition);
                }
                else if (ModifiedRecord is AnimatedObject @object)
                {
                    outputMod.AnimatedObjects.Add(@object);
                }
                else if (ModifiedRecord is Armor armor)
                {
                    outputMod.Armors.Add(armor);
                }
                else if (ModifiedRecord is ArmorAddon addon)
                {
                    outputMod.ArmorAddons.Add(addon);
                }
                else if (ModifiedRecord is ArtObject object1)
                {
                    outputMod.ArtObjects.Add(object1);
                }
                else if (ModifiedRecord is BodyPartData data)
                {
                    outputMod.BodyParts.Add(data);
                }
                else if (ModifiedRecord is Book book)
                {
                    outputMod.Books.Add(book);
                }
                else if (ModifiedRecord is CameraShot shot)
                {
                    outputMod.CameraShots.Add(shot);
                }
                else if (ModifiedRecord is Climate climate)
                {
                    outputMod.Climates.Add(climate);
                }
                else if (ModifiedRecord is Container container)
                {
                    outputMod.Containers.Add(container);
                }
                else if (ModifiedRecord is Door door)
                {
                    outputMod.Doors.Add(door);
                }
                else if (ModifiedRecord is Explosion explosion)
                {
                    outputMod.Explosions.Add(explosion);
                }
                else if (ModifiedRecord is Flora flora)
                {
                    if (flora.ActivateTextOverride != null && flora.ActivateTextOverride.TryLookup(PluginLanguage, out var localizedName))
                    {
                        flora.ActivateTextOverride = localizedName;
                    }
                    outputMod.Florae.Add(flora);
                }
                else if (ModifiedRecord is Furniture furniture)
                {
                    outputMod.Furniture.Add(furniture);
                }
                else if (ModifiedRecord is Grass grass)
                {
                    outputMod.Grasses.Add(grass);
                }
                else if (ModifiedRecord is Hazard hazard)
                {
                    outputMod.Hazards.Add(hazard);
                }
                else if (ModifiedRecord is HeadPart part)
                {
                    outputMod.HeadParts.Add(part);
                }
                else if (ModifiedRecord is IdleMarker marker)
                {
                    outputMod.IdleMarkers.Add(marker);
                }
                else if (ModifiedRecord is Impact impact)
                {
                    outputMod.Impacts.Add(impact);
                }
                else if (ModifiedRecord is Ingestible ingestible)
                {
                    outputMod.Ingestibles.Add(ingestible);
                }
                else if (ModifiedRecord is Ingredient ingredient)
                {
                    outputMod.Ingredients.Add(ingredient);
                }
                else if (ModifiedRecord is Key key)
                {
                    outputMod.Keys.Add(key);
                }
                else if (ModifiedRecord is LeveledNpc npc)
                {
                    outputMod.LeveledNpcs.Add(npc);
                }
                else if (ModifiedRecord is Light light)
                {
                    outputMod.Lights.Add(light);
                }
                else if (ModifiedRecord is MaterialObject object2)
                {
                    outputMod.MaterialObjects.Add(object2);
                }
                else if (ModifiedRecord is MiscItem item)
                {
                    outputMod.MiscItems.Add(item);
                }
                else if (ModifiedRecord is MoveableStatic rec_static)
                {
                    outputMod.MoveableStatics.Add(rec_static);
                }
                else if (ModifiedRecord is Projectile projectile)
                {
                    outputMod.Projectiles.Add(projectile);
                }
                else if (ModifiedRecord is Scroll scroll)
                {
                    outputMod.Scrolls.Add(scroll);
                }
                else if (ModifiedRecord is SoulGem gem)
                {
                    outputMod.SoulGems.Add(gem);
                }
                else if (ModifiedRecord is Static static1)
                {
                    outputMod.Statics.Add(static1);
                }
                else if (ModifiedRecord is TalkingActivator activator1)
                {
                    outputMod.TalkingActivators.Add(activator1);
                }
                else if (ModifiedRecord is Tree tree)
                {
                    outputMod.Trees.Add(tree);
                }
                else if (ModifiedRecord is Weapon weapon)
                {
                    outputMod.Weapons.Add(weapon);
                }
            }

            // New load order
            var newLo = Env.LoadOrder.ListedOrder.Select(x => x.ModKey);

            if (OutModNeeded)
            {
                newLo = newLo.And(OutMod.ModKey);
            }

            foreach (var mod in OutputSplitMods)
            {
                // Add each _X.esp plugin to the new load order
                newLo = newLo.And(mod.ModKey);
            }

            // Loop through each output split mod
            foreach (var mod in OutputSplitMods)
            {
                // Set ESL flag (these plugins can ALWAYS be ESL flagged because every record is an override)
                mod.IsSmallMaster = true;

                // Set ESM flag if user wants
                if (esmify == 1)
                {
                    mod.IsMaster = true;
                }

                // Write the output plugin
                if (OutModNeeded)
                {
                    mod.BeginWrite
                        .ToPath(Path.Combine(outputPath, mod.ModKey.FileName))
                        .WithLoadOrder(newLo)
                        .WithDataFolder(Env.DataFolderPath)
                        .WithExtraIncludedMasters(OutMod.ModKey)
                        .WithEmbeddedEncodings(new EncodingBundle(NonTranslated: MutagenEncoding._1252, NonLocalized: MutagenEncoding._utf8))
                        .Write();
                }
                else
                {
                    mod.BeginWrite
                        .ToPath(Path.Combine(outputPath, mod.ModKey.FileName))
                        .WithLoadOrder(newLo)
                        .WithDataFolder(Env.DataFolderPath)
                        .WithEmbeddedEncodings(new EncodingBundle(NonTranslated: MutagenEncoding._1252, NonLocalized: MutagenEncoding._utf8))
                        .Write();
                }
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetModelUses", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetModelUses(
      [DNNE.C99Type("const wchar_t*")] IntPtr modelPathPtr,
      [DNNE.C99Type("unsigned int*")] uint* length,
      [DNNE.C99Type("uint8_t**")] byte** bufferPtr)
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before GetModelUses");
            }

            if (length is null)
            {
                throw new Exception("length pointer is null");
            }

            var builder = new FlatBufferBuilder(1024);

            // Get the lowercase nifname (with meshes\ prefix) from C++
            string nifName = Marshal.PtrToStringUni(modelPathPtr)?.ToLower() ?? string.Empty;

            // find all uses
            if (!ModelUses.TryGetValue(nifName, out List<Tuple<FormKey, string>>? modelRecUsesList))
            {
                // No uses for this model
                *length = 0;
                return;
            }

            var modelUseOffsets = new List<Offset<PGMutagenBuffers.ModelUse>>();

            // loop through each use
            for (int i = 0; i < modelRecUsesList.Count; i++)
            {
                var formKey = modelRecUsesList[i].Item1;
                var subModel = modelRecUsesList[i].Item2;

                // Try to resolve the model record and submodel
                if (!Env.LinkCache.TryResolve<IMajorRecordGetter>(formKey, out var modelRec) ||
                    GetModelElemBySubModel(modelRec, subModel) is not { } matchedModel)
                {
                    if (formKeyErrorsPosted.Add(formKey))
                    {
                        MessageHandler.Log($"Failed to resolve model record. A plugin has likely overridden the FormID with a different record (This is bad): {GetRecordDesc(formKey)}", 4);
                    }
                    continue;
                }

                // find alternate textures
                if (matchedModel.AlternateTextures is null)
                {
                    continue;
                }

                var altTexOffsets = new List<Offset<PGMutagenBuffers.AlternateTexture>>();

                for (int j = 0; j < matchedModel.AlternateTextures.Count; j++)
                {
                    var altTexIdx = matchedModel.AlternateTextures[j].Index;
                    var newTXST = matchedModel.AlternateTextures[j].NewTexture;

                    var textureSetOffsets = new List<Offset<PGMutagenBuffers.TextureSet>>();

                    // find newTXST record
                    var textures = new string[8];
                    if (Env.LinkCache.TryResolve<ITextureSetGetter>(newTXST.FormKey, out var newTXSTRec))
                    {
                        // The 8 strings in textureset are:
                        // newTXSTRec.Texture1 - Texture8
                        textures = GetTextureSet(newTXSTRec);
                        for (int k = 0; k < 8; k++)
                        {
                            if (textures[k].IsNullOrEmpty())
                            {
                                continue;
                            }

                            textures[k] = AddPrefixIfNotExists("textures\\", textures[k]);
                        }
                    }
                    else
                    {
                        // the 8 strings in the textureset should exist but all be empty
                        textures = [.. Enumerable.Repeat(string.Empty, 8)];
                    }

                    // Build the TextureSet
                    var textureSetVec = PGMutagenBuffers.TextureSet.CreateTexturesVector(
                        builder,
                        [.. textures.Select(s => builder.CreateString(s))]
                    );

                    PGMutagenBuffers.TextureSet.StartTextureSet(builder);
                    PGMutagenBuffers.TextureSet.AddTextures(builder, textureSetVec);
                    var textureSetOffset = PGMutagenBuffers.TextureSet.EndTextureSet(builder);

                    // Build the AlternateTexture (slots now holds a single TextureSet, not a vector)
                    PGMutagenBuffers.AlternateTexture.StartAlternateTexture(builder);
                    PGMutagenBuffers.AlternateTexture.AddSlotId(builder, altTexIdx);
                    // add slot_id_new if you have a value for it
                    PGMutagenBuffers.AlternateTexture.AddSlots(builder, textureSetOffset);
                    var altTexOffset = PGMutagenBuffers.AlternateTexture.EndAlternateTexture(builder);

                    altTexOffsets.Add(altTexOffset);
                }

                // check if this is IStaticGetter for materials
                bool is_singlePass = false;
                if (modelRec is IStaticGetter staticRec && Env.LinkCache.TryResolve<IMaterialObjectGetter>(staticRec.Material.FormKey, out var materialRec))
                {
                    is_singlePass = (materialRec.Flags & MaterialObject.Flag.SinglePass) != 0;
                }

                var altTexVector = PGMutagenBuffers.ModelUse.CreateAlternateTexturesVector(builder, [.. altTexOffsets]);
                var modNameOffset = builder.CreateString(formKey.ModKey.FileName);
                var subModelOffset = builder.CreateString(subModel);
                var modelNameOffset = builder.CreateString(nifName);

                PGMutagenBuffers.ModelUse.StartModelUse(builder);
                PGMutagenBuffers.ModelUse.AddModName(builder, modNameOffset);
                PGMutagenBuffers.ModelUse.AddFormId(builder, formKey.ID);
                PGMutagenBuffers.ModelUse.AddSubModel(builder, subModelOffset);
                PGMutagenBuffers.ModelUse.AddMeshFile(builder, modelNameOffset);
                PGMutagenBuffers.ModelUse.AddSinglepassMato(builder, is_singlePass);
                PGMutagenBuffers.ModelUse.AddAlternateTextures(builder, altTexVector);
                var modelUseOffset = PGMutagenBuffers.ModelUse.EndModelUse(builder);

                modelUseOffsets.Add(modelUseOffset);
            }

            var usesVector = PGMutagenBuffers.ModelUses.CreateUsesVector(builder, [.. modelUseOffsets]);
            PGMutagenBuffers.ModelUses.StartModelUses(builder);
            PGMutagenBuffers.ModelUses.AddUses(builder, usesVector);
            var rootOffset = PGMutagenBuffers.ModelUses.EndModelUses(builder); // returns Offset<ModelUses>
                                                                               //MessageHandler.Log("Serialized model uses count: " + rootOffset.Value, 4);
            builder.Finish(rootOffset.Value);

            var byteArray = builder.SizedByteArray(); // fully serialized FlatBuffer
            *length = (uint)byteArray.Length;

            // Allocate memory for C++ side
            *bufferPtr = (byte*)Marshal.AllocHGlobal(byteArray.Length);
            Marshal.Copy(byteArray, 0, (IntPtr)(*bufferPtr), byteArray.Length);
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "SetModelUses", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void SetModelUses(
      [DNNE.C99Type("const unsigned int")] uint length,
      [DNNE.C99Type("const uint8_t*")] byte* bufferPtr)
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before SetModelUses");
            }

            if (OutMod is null)
            {
                throw new Exception("OutMod is null in SetModelUses");
            }

            if (length == 0 || bufferPtr == null)
            {
                return;
            }

            // Convert bufferPtr to span
            Span<byte> bufferSpan = new(bufferPtr, (int)length);

            // Load onto buffer
            var buffer = new ByteBuffer(bufferSpan.ToArray());
            var modelUses = PGMutagenBuffers.ModelUses.GetRootAsModelUses(buffer);

            // Loop through each model use
            for (int i = 0; i < modelUses.UsesLength; i++)
            {
                var modelUseContainer = modelUses.Uses(i);
                if (!modelUseContainer.HasValue)
                {
                    continue;
                }

                var modelUse = modelUseContainer.Value;
                var searchFormKey = new FormKey(modelUse.ModName, modelUse.FormId);

                // Find winning record for this formkey
                if (!Env.LinkCache.TryResolve<IMajorRecordGetter>(searchFormKey, out var existingRecord))
                {
                    // Record doesn't exist, skip
                    throw new Exception("Failed to resolve model record for formkey: " + searchFormKey);
                }

                // check if we already modified this record
                IMajorRecord modRecord;
                if (ModifiedRecords.TryGetValue(searchFormKey, out IMajorRecord? value))
                {
                    // record already modified
                    modRecord = value;
                }
                else
                {
                    modRecord = existingRecord.DeepCopy();
                }

                // Find model record to modify
                var matchExistingElem = GetModelElemBySubModel(existingRecord, modelUse.SubModel);
                var matchModElem = GetModelElemBySubModel(modRecord, modelUse.SubModel);
                if (matchExistingElem is null || matchModElem is null)
                {
                    throw new Exception("Failed to find submodel: " + modelUse.SubModel + " in record: " + GetRecordDesc(existingRecord));
                }

                if (ProcessedModelUses.Contains(matchExistingElem))
                {
                    // already processed this model use, skip
                    continue;
                }

                ProcessedModelUses.Add(matchExistingElem);

                // Actual changes starting
                bool changed = false;

                // Mesh path
                var newMeshFile = RemovePrefixIfExists("meshes\\", modelUse.MeshFile);
                if (!string.Equals(matchExistingElem.File, newMeshFile, StringComparison.OrdinalIgnoreCase))
                {
                    // Change mesh path
                    matchModElem.File = newMeshFile;
                    changed = true;
                }

                if (matchExistingElem.AlternateTextures is null || matchModElem.AlternateTextures is null)
                {
                    // Not allowed to modify alternate textures that do not exist
                    continue;
                }

                // Create dictionary of old alternate texture idx to buffer entry
                Dictionary<int, PGMutagenBuffers.AlternateTexture> altTexDict = [];
                for (int j = 0; j < modelUse.AlternateTexturesLength; j++)
                {
                    var curAltTex = modelUse.AlternateTextures(j);
                    if (!curAltTex.HasValue)
                    {
                        continue;
                    }

                    altTexDict[curAltTex.Value.SlotId] = curAltTex.Value;
                }
                // Loop through existing alternate textures
                for (int j = 0; j < matchExistingElem.AlternateTextures.Count; j++)
                {
                    var curAltTex = matchExistingElem.AlternateTextures[j];
                    if (!altTexDict.ContainsKey(curAltTex.Index))
                    {
                        continue;
                    }

                    // Found matching alternate texture, update it if required
                    var bufAltTex = altTexDict[curAltTex.Index];

                    // Index
                    var newAltTex = bufAltTex.SlotIdNew;
                    if (curAltTex.Index != newAltTex)
                    {
                        // Change index
                        matchModElem.AlternateTextures[j].Index = newAltTex;
                        changed = true;
                    }

                    // Find new texture set
                    if (bufAltTex.Slots is null || !bufAltTex.Slots.HasValue || bufAltTex.Slots.Value.TexturesLength != 8)
                    {
                        // No texture set, skip
                        continue;
                    }

                    // get texture set array from buffer
                    string[] bufTextures = [
                        bufAltTex.Slots.Value.Textures(0) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(1) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(2) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(3) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(4) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(5) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(6) ?? string.Empty,
                        bufAltTex.Slots.Value.Textures(7) ?? string.Empty
                    ];

                    string[] existingTextures;
                    // find existing texture set record
                    if (Env.LinkCache.TryResolve<ITextureSetGetter>(curAltTex.NewTexture.FormKey, out var existingTXSTRec))
                    {
                        existingTextures = GetTextureSet(existingTXSTRec);
                    }
                    else
                    {
                        existingTextures = [.. Enumerable.Repeat(string.Empty, 8)];
                    }

                    // check if textures are different
                    if (existingTextures.SequenceEqual(bufTextures, StringComparer.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    // Textures are different, we need to find or create a new texture set record
                    changed = true;
                    if (NewTextureSets.TryGetValue(bufTextures, out Tuple<ITextureSet, bool>? existingTXSTTuple))
                    {
                        // already exists, just use that
                        matchModElem.AlternateTextures[j].NewTexture.FormKey = existingTXSTTuple.Item1.FormKey;

                        // Update usage flag
                        if (!existingTXSTTuple.Item2)
                        {
                            NewTextureSets[bufTextures] = new Tuple<ITextureSet, bool>(existingTXSTTuple.Item1, true);
                        }
                    }
                    else
                    {
                        // Create a new texture set record
                        var newFormKey = new FormKey(OutMod.ModKey, GetLowestAvailableFormID());
                        // find filename of diffuse texture (just .dds file no path), also remove extension
                        var diffuseTex = bufTextures[0].IsNullOrEmpty() ? "" : Path.GetFileNameWithoutExtension(bufTextures[0]);
                        var formIDHex = newFormKey.ID.ToString("X6");
                        var newEDID = "PG_";
                        if (!diffuseTex.IsNullOrEmpty())
                        {
                            newEDID += diffuseTex + "_" + formIDHex;
                        }
                        else
                        {
                            newEDID += formIDHex;
                        }

                        var newTXSTObj = new TextureSet(newFormKey, Env.GameRelease.ToSkyrimRelease())
                        {
                            Diffuse = bufTextures[0].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[0]),
                            NormalOrGloss = bufTextures[1].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[1]),
                            GlowOrDetailMap = bufTextures[2].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[2]),
                            Height = bufTextures[3].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[3]),
                            Environment = bufTextures[4].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[4]),
                            EnvironmentMaskOrSubsurfaceTint = bufTextures[5].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[5]),
                            Multilayer = bufTextures[6].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[6]),
                            BacklightMaskOrSpecular = bufTextures[7].IsNullOrEmpty() ? null : RemovePrefixIfExists("textures\\", bufTextures[7]),
                            EditorID = newEDID
                        };

                        // Add to output mod
                        OutMod.TextureSets.Add(newTXSTObj);
                        allocatedFormIDs.Add(newFormKey.ID);
                        lastUsedFormID = newFormKey.ID;

                        // Add to dictionary
                        NewTextureSets[bufTextures] = new Tuple<ITextureSet, bool>(newTXSTObj, true);

                        // Update formkey
                        matchModElem.AlternateTextures[j].NewTexture.FormKey = newFormKey;
                    }
                }

                // add to modified records only if something has changed
                if (changed)
                {
                    ModifiedRecords[searchFormKey] = modRecord;
                }
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    //
    // Helpers
    //

    private static SkyrimMod GetModToAdd(IMajorRecord majorRecord)
    {
        if (Env is null)
        {
            throw new Exception("Initialize must be called before getModToAdd");
        }

        // curMasterList is the masters needed for the current record (including the record itself)
        var curMasterList = majorRecord.EnumerateFormLinks().Select(x => x.FormKey.ModKey).ToHashSet();
        curMasterList.Add(majorRecord.FormKey.ModKey);

        // check if this modkey is in any of the existing output plugins
        for (int i = 0; i < OutputSplitMods.Count; i++)
        {
            // check if curMasterList is a subset of the masters in the current plugin
            if (curMasterList.IsSubsetOf(OutputMasterTracker[i]))
            {
                // if it is, the current plugin can be used as there is no other masters to add
                return OutputSplitMods[i];
            }

            // check what the result would be if we added the current record's modkey to the plugin
            var newMasterList = new HashSet<ModKey>(OutputMasterTracker[i]);
            newMasterList.UnionWith(curMasterList);
            if (newMasterList.Count < 254)
            {
                // If the new master list is still less than 254, then we can use it
                // We must update the master list of the current plugin for next loop
                OutputMasterTracker[i] = newMasterList;
                return OutputSplitMods[i];
            }
        }

        // we need to create a new plugin
        var newPluginIndex = OutputSplitMods.Count + 1;
        var newPluginName = "PG_" + newPluginIndex + ".esp";
        var newMod = new SkyrimMod(newPluginName, GameType);

        // add the new modkey to the new plugin
        OutputMasterTracker.Add(curMasterList);
        OutputSplitMods.Add(newMod);

        return OutputSplitMods[newPluginIndex - 1];
    }

    private static List<Tuple<IModelGetter, string>> GetModelElems(IMajorRecordGetter Rec)
    {
        // Will store models to check later
        List<Tuple<IModelGetter, string>> ModelRecs = [];

        // Figure out special cases for nested models
        if (Rec is IArmorGetter armorObj && armorObj.WorldModel is not null)
        {
            if (armorObj.WorldModel.Male is not null && armorObj.WorldModel.Male.Model is not null)
            {
                ModelRecs.Add(new Tuple<IModelGetter, string>(armorObj.WorldModel.Male.Model, "MALE"));
            }

            if (armorObj.WorldModel.Female is not null && armorObj.WorldModel.Female.Model is not null)
            {
                ModelRecs.Add(new Tuple<IModelGetter, string>(armorObj.WorldModel.Female.Model, "FEMALE"));
            }
        }
        else if (Rec is IArmorAddonGetter armorAddonObj)
        {
            if (armorAddonObj.WorldModel is not null)
            {
                if (armorAddonObj.WorldModel.Male is not null)
                {
                    ModelRecs.Add(new Tuple<IModelGetter, string>(armorAddonObj.WorldModel.Male, "MALE"));
                }

                if (armorAddonObj.WorldModel.Female is not null)
                {
                    ModelRecs.Add(new Tuple<IModelGetter, string>(armorAddonObj.WorldModel.Female, "FEMALE"));
                }
            }

            if (armorAddonObj.FirstPersonModel is not null)
            {
                if (armorAddonObj.FirstPersonModel.Male is not null)
                {
                    ModelRecs.Add(new Tuple<IModelGetter, string>(armorAddonObj.FirstPersonModel.Male, "1STMALE"));
                }

                if (armorAddonObj.FirstPersonModel.Female is not null)
                {
                    ModelRecs.Add(new Tuple<IModelGetter, string>(armorAddonObj.FirstPersonModel.Female, "1STFEMALE"));
                }
            }
        }
        else if (Rec is IModeledGetter modeledObj && modeledObj.Model is not null)
        {
            ModelRecs.Add(new Tuple<IModelGetter, string>(modeledObj.Model, "MODL"));
        }

        return ModelRecs;
    }

    private static List<Tuple<IModel, string>> GetModelElems(IMajorRecord Rec)
    {
        // Will store models to check later
        List<Tuple<IModel, string>> ModelRecs = [];

        // Figure out special cases for nested models
        if (Rec is IArmor armorObj && armorObj.WorldModel is not null)
        {
            if (armorObj.WorldModel.Male is not null && armorObj.WorldModel.Male.Model is not null)
            {
                ModelRecs.Add(new Tuple<IModel, string>(armorObj.WorldModel.Male.Model, "MALE"));
            }

            if (armorObj.WorldModel.Female is not null && armorObj.WorldModel.Female.Model is not null)
            {
                ModelRecs.Add(new Tuple<IModel, string>(armorObj.WorldModel.Female.Model, "FEMALE"));
            }
        }
        else if (Rec is IArmorAddon armorAddonObj)
        {
            if (armorAddonObj.WorldModel is not null)
            {
                if (armorAddonObj.WorldModel.Male is not null)
                {
                    ModelRecs.Add(new Tuple<IModel, string>(armorAddonObj.WorldModel.Male, "MALE"));
                }

                if (armorAddonObj.WorldModel.Female is not null)
                {
                    ModelRecs.Add(new Tuple<IModel, string>(armorAddonObj.WorldModel.Female, "FEMALE"));
                }
            }

            if (armorAddonObj.FirstPersonModel is not null)
            {
                if (armorAddonObj.FirstPersonModel.Male is not null)
                {
                    ModelRecs.Add(new Tuple<IModel, string>(armorAddonObj.FirstPersonModel.Male, "1STMALE"));
                }

                if (armorAddonObj.FirstPersonModel.Female is not null)
                {
                    ModelRecs.Add(new Tuple<IModel, string>(armorAddonObj.FirstPersonModel.Female, "1STFEMALE"));
                }
            }
        }
        else if (Rec is IModeled modeledObj && modeledObj.Model is not null)
        {
            ModelRecs.Add(new Tuple<IModel, string>(modeledObj.Model, "MODL"));
        }

        return ModelRecs;
    }

    private static IModel? GetModelElemBySubModel(IMajorRecord Rec, string SubModel)
    {
        var ModelRecs = GetModelElems(Rec);

        foreach (var modelRec in ModelRecs)
        {
            if (modelRec.Item2 == SubModel)
            {
                return modelRec.Item1;
            }
        }

        return null;
    }

    private static IModelGetter? GetModelElemBySubModel(IMajorRecordGetter Rec, string SubModel)
    {
        var ModelRecs = GetModelElems(Rec);

        foreach (var modelRec in ModelRecs)
        {
            if (modelRec.Item2 == SubModel)
            {
                return modelRec.Item1;
            }
        }

        return null;
    }

    private static IEnumerable<IMajorRecordGetter> EnumerateModelRecords()
    {
        if (Env is null)
        {
            return [];
        }

        return Env.LoadOrder.PriorityOrder.Activator().WinningOverrides()
                .Concat<IMajorRecordGetter>(Env.LoadOrder.PriorityOrder.AddonNode().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Ammunition().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.AnimatedObject().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Armor().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.ArmorAddon().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.ArtObject().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.BodyPartData().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Book().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.CameraShot().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Climate().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Container().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Door().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Explosion().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Flora().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Furniture().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Grass().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Hazard().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.HeadPart().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.IdleMarker().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Impact().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Ingestible().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Ingredient().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Key().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.LeveledNpc().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Light().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.MaterialObject().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.MiscItem().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.MoveableStatic().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Projectile().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Scroll().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.SoulGem().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Static().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.TalkingActivator().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Tree().WinningOverrides())
                .Concat(Env.LoadOrder.PriorityOrder.Weapon().WinningOverrides());
    }

    private static IEnumerable<IMajorRecordGetter> EnumerateModelRecordsSafe()
    {
        using (var enumerator = EnumerateModelRecords().GetEnumerator())
        {
            bool next = true;

            while (next)
            {
                try
                {
                    next = enumerator.MoveNext();
                }
                catch (RecordException ex)
                {
                    var innerEx = ex.InnerException;
                    if (innerEx is null)
                    {
                        MessageHandler.Log("Unknown error with mod " + ex.ModKey, 3);
                    }
                    else
                    {
                        MessageHandler.Log(innerEx.ToString(), 3);
                    }

                    continue;
                }

                if (next)
                    yield return enumerator.Current;
            }
        }
    }

    private static string[] GetTextureSet(ITextureSetGetter textureSet)
    {
        try
        {
            return
            [
                textureSet.Diffuse?.ToString().ToLower() ?? string.Empty,
                textureSet.NormalOrGloss?.ToString().ToLower() ?? string.Empty,
                textureSet.GlowOrDetailMap?.ToString().ToLower() ?? string.Empty,
                textureSet.Height?.ToString().ToLower() ?? string.Empty,
                textureSet.Environment?.ToString().ToLower() ?? string.Empty,
                textureSet.EnvironmentMaskOrSubsurfaceTint?.ToString().ToLower() ?? string.Empty,
                textureSet.Multilayer?.ToString().ToLower() ?? string.Empty,
                textureSet.BacklightMaskOrSpecular?.ToString().ToLower() ?? string.Empty
            ];
        }
        catch (Exception)
        {
            if (formKeyErrorsPosted.Add(textureSet.FormKey))
            {
                MessageHandler.Log("Unable to read texture set. This should be reported to the plugin author: " + GetRecordDesc(textureSet), 4);
            }
            return [.. Enumerable.Repeat(string.Empty, 8)];
        }
    }

    private static string GetRecordDesc(IMajorRecordGetter rec)
    {
        return rec.FormKey.ModKey.FileName + " / " + rec.FormKey.ID.ToString("X6");
    }

    private static string GetRecordDesc(FormKey rec)
    {
        return rec.ModKey.FileName + " / " + rec.ID.ToString("X6");
    }

    private static string RemovePrefixIfExists(string prefix, string str)
    {
        if (str.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            return str[prefix.Length..];
        }
        return str;
    }

    private static string AddPrefixIfNotExists(string prefix, string str)
    {
        if (!str.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            return prefix + str;
        }
        return str;
    }

    private static uint GetLowestAvailableFormID()
    {
        for (uint id = lastUsedFormID + 1; id <= 0xFFFFFF; id++)
        {
            if (!allocatedFormIDs.Contains(id))
            {
                return id;
            }
        }

        throw new Exception("No available FormIDs left in plugin");
    }
}


// HARMONY PATCHES

[HarmonyPatch(typeof(AppDomain))]
[HarmonyPatch("get_BaseDirectory")]
public static class PatchBaseDirectory
{
    public static string? BaseDirectory;

    public static bool Prefix(ref string __result)
    {
        __result = BaseDirectory ?? string.Empty;

        // don't invoke original method
        return false;
    }
}
