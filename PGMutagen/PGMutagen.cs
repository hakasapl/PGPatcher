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

public class PGMutagen
{
    // "Class vars" actually static because p/invoke doesn't support instance methods
    private static SkyrimMod? OutMod;
    private static IGameEnvironment<ISkyrimMod, ISkyrimModGetter>? Env;
    private static List<ITextureSetGetter> TXSTObjs = [];
    private static List<Tuple<IAlternateTextureGetter, int, int, string, string, uint>> AltTexRefs = [];
    private static List<IMajorRecordGetter> ModelOriginals = [];
    private static List<IMajorRecord> ModelCopies = [];
    private static List<IMajorRecord> ModelCopiesEditable = [];
    private static Dictionary<Tuple<string, int>, List<Tuple<int, int, string, uint>>>? TXSTRefs;
    private static HashSet<int> ModifiedModeledRecords = [];
    private static SkyrimRelease GameType;

    private static uint maxFormID = 0;

    // tracks masters in each split plugin
    private static List<HashSet<ModKey>> OutputMasterTracker = [];
    private static List<SkyrimMod> OutputSplitMods = [];

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

    [UnmanagedCallersOnly(EntryPoint = "Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void Initialize(
      [DNNE.C99Type("const int")] int gameType,
      [DNNE.C99Type("const wchar_t*")] IntPtr exePath,
      [DNNE.C99Type("const wchar_t*")] IntPtr dataPathPtr,
      [DNNE.C99Type("const wchar_t**")] IntPtr* loadOrder)
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

            GameType = (SkyrimRelease)gameType;
            OutMod = new SkyrimMod(ModKey.FromFileName("ParallaxGen.esp"), GameType);

            List<ModKey> loadOrderList = [];
            for (int i = 0; loadOrder[i] != IntPtr.Zero; i++)
            {
                loadOrderList.Add(Marshal.PtrToStringUni(loadOrder[i]) ?? string.Empty);
            }

            try
            {
                Env = GameEnvironment.Typical.Builder<ISkyrimMod, ISkyrimModGetter>((GameRelease)GameType)
                    .WithTargetDataFolder(dataPath)
                    .WithLoadOrder(loadOrderList.ToArray())
                    .WithOutputMod(OutMod)
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

    [UnmanagedCallersOnly(EntryPoint = "PopulateObjs", CallConvs = [typeof(CallConvCdecl)])]
    public static void PopulateObjs()
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before PopulateObjs");
            }

            //
            // 1. Add all TXST records in the load order to TXSTObjs
            //
            TXSTObjs = [];
            foreach (var textureSet in Env.LoadOrder.PriorityOrder.TextureSet().WinningOverrides())
            {
                TXSTObjs.Add(textureSet);
            }

            //
            // 2. Add all MODL records in the load order to ModelOriginals
            //
            TXSTRefs = [];
            // model rec counter is simply a unique identifier for each model record to differentiate them. It is not used as a handle.
            int ModelRecCounter = 0;
            // DCIdx is the current index in ModelCopes, ModelCopiesEditable, and ModelOriginals
            var DCIdx = -1;
            foreach (var txstRefObj in EnumerateModelRecordsSafe())
            {
                // Will store models to check later
                var ModelRecs = GetModelElems(txstRefObj);

                if (ModelRecs.Count == 0)
                {
                    // Skip if there are no MODL records in this record
                    continue;
                }

                bool CopiedRecord = false;
                foreach (var modelRec in ModelRecs)
                {
                    if (modelRec.Item1.AlternateTextures is null)
                    {
                        // no alternate textures in this MODL record, we can skip
                        continue;
                    }

                    if (!CopiedRecord)
                    {
                        // Deep copy major record (it has not been copied yet)
                        try
                        {
                            // Run deep copies
                            var DeepCopy = txstRefObj.DeepCopy();
                            var DeepCopyEditable = txstRefObj.DeepCopy();

                            // Store original parent record
                            ModelOriginals.Add(txstRefObj);

                            // Save model copies
                            ModelCopies.Add(DeepCopy);
                            ModelCopiesEditable.Add(DeepCopyEditable);
                            DCIdx = ModelCopies.Count - 1;
                            CopiedRecord = true;
                        }
                        catch (Exception)
                        {
                            MessageHandler.Log("Failed to copy record: " + GetRecordDesc(txstRefObj), 3);
                            break;
                        }
                    }

                    // find lowercase nifname
                    string nifName = modelRec.Item1.File;
                    nifName = RemovePrefixIfExists("\\", nifName);
                    nifName = nifName.ToLower();
                    nifName = AddPrefixIfNotExists("meshes\\", nifName);

                    // loop through each alternate texture
                    foreach (var alternateTexture in modelRec.Item1.AlternateTextures)
                    {
                        // Add to global
                        var AltTexEntry = new Tuple<IAlternateTextureGetter, int, int, string, string, uint>(alternateTexture, DCIdx, ModelRecCounter, modelRec.Item2, txstRefObj.FormKey.ModKey.ToString(), txstRefObj.FormKey.ID);
                        AltTexRefs.Add(AltTexEntry);
                        var AltTexId = AltTexRefs.Count - 1;

                        // index3D is the alternate texture index 3d in the recorc
                        int index3D = alternateTexture.Index;
                        // texture set record used for alternate texture
                        var newTXST = alternateTexture.NewTexture;

                        // create lookup key
                        var key = new Tuple<string, int>(nifName, index3D);

                        // find the index of the TXST record in the TXSTObjs list (this is a handle)
                        int newTXSTIndex = TXSTObjs.FindIndex(x => x.FormKey == newTXST.FormKey);
                        if (newTXSTIndex < 0)
                        {
                            // TXST record doesn't exist, skip
                            continue;
                        }

                        // Add txstrefs key to list if it doesn't exist
                        var newTXSTObj = TXSTObjs[newTXSTIndex];
                        if (!TXSTRefs.ContainsKey(key))
                        {
                            TXSTRefs[key] = [];
                        }

                        // Add txst reference with TXST handle and alternate texture handle
                        TXSTRefs[key].Add(new Tuple<int, int, string, uint>(newTXSTIndex, AltTexId, newTXSTObj.FormKey.ModKey.ToString(), newTXSTObj.FormKey.ID));
                    }

                    // increment modelreccounter
                    ModelRecCounter++;
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

            // Get output path from C++
            string outputPath = Marshal.PtrToStringUni(outputPathPtr) ?? string.Empty;

            // Check if OutMod can be ESL flagged
            if (maxFormID <= 0xFFF)
            {
                OutMod.IsSmallMaster = true;
            }

            // Flag as ESM if user wants
            if (esmify == 1)
            {
                OutMod.IsMaster = true;
            }

            if (!OutMod.EnumerateMajorRecords().Any())
            {
                // No records in OutMod, exit.
                return;
            }

            // Write OutMod
            // We disable formid compactness check for VR specifically as we require Skyrim VR ESL support mod so we CAN use those extra formids
            OutMod.BeginWrite
                .ToPath(Path.Combine(outputPath, OutMod.ModKey.FileName))
                .WithLoadOrder(Env.LoadOrder)
                .NoFormIDCompactnessCheck()
                .Write();

            // Add all modified model records to the output mod
            foreach (var recId in ModifiedModeledRecords)
            {
                var ModifiedRecord = ModelCopiesEditable[recId];

                var outputMod = getModToAdd(ModifiedRecord);

                if (ModifiedRecord is Mutagen.Bethesda.Skyrim.Activator activator)
                {
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
                else if (ModifiedRecord is MoveableStatic @static)
                {
                    outputMod.MoveableStatics.Add(@static);
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

            // Add OutMod to a new load order for saving other plugins
            var newLo = Env.LoadOrder.ListedOrder.Select(x => x.ModKey).And(OutMod.ModKey);
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
                mod.BeginWrite
                    .ToPath(Path.Combine(outputPath, mod.ModKey.FileName))
                    .WithLoadOrder(newLo)
                    .WithDataFolder(Env.DataFolderPath)
                    .WithExtraIncludedMasters(OutMod.ModKey)
                    .Write();
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    private static SkyrimMod getModToAdd(IMajorRecord majorRecord)
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

    [UnmanagedCallersOnly(EntryPoint = "GetMatchingTXSTObjs", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetMatchingTXSTObjs(
      [DNNE.C99Type("const wchar_t*")] IntPtr nifNamePtr,
      [DNNE.C99Type("const int")] int index3D,
      [DNNE.C99Type("int*")] int* TXSTHandles,
      [DNNE.C99Type("int*")] int* AltTexHandles,
      [DNNE.C99Type("wchar_t**")] IntPtr* MatchedNIF,
      [DNNE.C99Type("char**")] IntPtr* MatchedType,
      [DNNE.C99Type("wchar_t**")] IntPtr* AltTexModKey,
      [DNNE.C99Type("unsigned int*")] uint* AltTexFormID,
      [DNNE.C99Type("wchar_t**")] IntPtr* TXSTModKey,
      [DNNE.C99Type("unsigned int*")] uint* TXSTFormID,
      [DNNE.C99Type("int*")] int* length)
    {
        try
        {
            if (TXSTRefs is null)
            {
                throw new Exception("PopulateObjs must be called before GetMatchingTXSTObjs");
            }

            // This function has 2 modes. When length is not null, the caller wants the length of the output arrays.
            if (length is not null)
            {
                *length = 0;
            }

            // Get the lowercase nifname (with meshes\ prefix) from C++
            string nifName = Marshal.PtrToStringUni(nifNamePtr) ?? string.Empty;
            nifName = nifName.ToLower();

            // Create lookup key
            var key = new Tuple<string, int>(nifName, index3D);

            // Create txstList, which is an output list of matching TXST objects
            List<Tuple<int, int, string, string, string, uint, string>> txstList = [];
            List<Tuple<uint>> txstList2 = [];
            if (TXSTRefs.TryGetValue(key, out List<Tuple<int, int, string, uint>>? value))
            {
                foreach (var txst in value)
                {
                    txstList.Add(new Tuple<int, int, string, string, string, uint, string>(txst.Item1, txst.Item2, nifName, AltTexRefs[txst.Item2].Item4, AltTexRefs[txst.Item2].Item5, AltTexRefs[txst.Item2].Item6, txst.Item3));
                    txstList2.Add(new Tuple<uint>(txst.Item4));
                }
            }

            // find alternate nif Name if able
            // The _0 and _1 are implicit for some armors so we need to check both
            string altNifName = "";
            if (nifName.EndsWith("_1.nif"))
            {
                // replace _1.nif with _0.nif
                altNifName = nifName.Substring(0, nifName.Length - 6) + "_0.nif";
            }

            if (nifName.EndsWith("_0.nif"))
            {
                // replace _0.nif with _1.nif
                altNifName = nifName.Substring(0, nifName.Length - 6) + "_1.nif";
            }

            // Alternate key to lookup
            var altKey = new Tuple<string, int>(altNifName, index3D);
            if (TXSTRefs.TryGetValue(altKey, out List<Tuple<int, int, string, uint>>? valueAlt))
            {
                foreach (var txst in valueAlt)
                {
                    var altTexRef = AltTexRefs[txst.Item2];
                    if (altTexRef.Item4 == "MODL")
                    {
                        // Skip if not _1 _0 type of MODL (ensure we only match stuff that actually needs to be matched)
                        continue;
                    }

                    txstList.Add(new Tuple<int, int, string, string, string, uint, string>(txst.Item1, txst.Item2, altNifName, altTexRef.Item4, altTexRef.Item5, altTexRef.Item6, txst.Item3));
                    txstList2.Add(new Tuple<uint>(txst.Item4));
                }
            }

            if (length is not null)
            {
                // Set length to the count of matching TXST objects
                *length = txstList.Count;
            }

            if (TXSTHandles is null || AltTexHandles is null || MatchedNIF is null || MatchedType is null || AltTexModKey is null || TXSTModKey is null)
            {
                // If any of the output pointers are null, return immediately
                return;
            }

            // Manually copy the elements from the list to the pointer
            for (int i = 0; i < txstList.Count; i++)
            {
                // Assign the output values for C++ to read
                TXSTHandles[i] = txstList[i].Item1;
                AltTexHandles[i] = txstList[i].Item2;
                MatchedNIF[i] = txstList[i].Item3.IsNullOrEmpty() ? IntPtr.Zero : Marshal.StringToHGlobalUni(txstList[i].Item3);
                MatchedType[i] = txstList[i].Item4.IsNullOrEmpty() ? IntPtr.Zero : Marshal.StringToHGlobalAnsi(txstList[i].Item4);
                AltTexModKey[i] = txstList[i].Item5.IsNullOrEmpty() ? IntPtr.Zero : Marshal.StringToHGlobalUni(txstList[i].Item5);
                AltTexFormID[i] = txstList[i].Item6;
                TXSTModKey[i] = txstList[i].Item7.IsNullOrEmpty() ? IntPtr.Zero : Marshal.StringToHGlobalUni(txstList[i].Item7);
                TXSTFormID[i] = txstList2[i].Item1;
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetTXSTSlots", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetTXSTSlots(
      [DNNE.C99Type("const int")] int txstIndex,
      [DNNE.C99Type("wchar_t**")] IntPtr* slotsArray)
    {
        try
        {
            if (TXSTObjs == null || txstIndex < 0 || txstIndex >= TXSTObjs.Count)
            {
                throw new Exception("Invalid TXST index or not populated");
            }

            // Pull the TXST object from the list
            var txstObj = TXSTObjs[txstIndex];

            // Populate the slotsArray with string pointers
            try
            {
                if (!txstObj.Diffuse.IsNullOrEmpty())
                {
                    var Diffuse = AddPrefixIfNotExists("textures\\", txstObj.Diffuse).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] Diffuse: " + Diffuse, 0);
                    slotsArray[0] = Marshal.StringToHGlobalUni(Diffuse);
                }
                if (!txstObj.NormalOrGloss.IsNullOrEmpty())
                {
                    var NormalOrGloss = AddPrefixIfNotExists("textures\\", txstObj.NormalOrGloss).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] NormalOrGloss: " + NormalOrGloss, 0);
                    slotsArray[1] = Marshal.StringToHGlobalUni(NormalOrGloss);
                }
                if (!txstObj.GlowOrDetailMap.IsNullOrEmpty())
                {
                    var GlowOrDetailMap = AddPrefixIfNotExists("textures\\", txstObj.GlowOrDetailMap).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] GlowOrDetailMap: " + GlowOrDetailMap, 0);
                    slotsArray[2] = Marshal.StringToHGlobalUni(GlowOrDetailMap);
                }
                if (!txstObj.Height.IsNullOrEmpty())
                {
                    var Height = AddPrefixIfNotExists("textures\\", txstObj.Height).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] Height: " + Height, 0);
                    slotsArray[3] = Marshal.StringToHGlobalUni(Height);
                }
                if (!txstObj.Environment.IsNullOrEmpty())
                {
                    var Environment = AddPrefixIfNotExists("textures\\", txstObj.Environment).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] Environment: " + Environment, 0);
                    slotsArray[4] = Marshal.StringToHGlobalUni(Environment);
                }
                if (!txstObj.EnvironmentMaskOrSubsurfaceTint.IsNullOrEmpty())
                {
                    var EnvironmentMaskOrSubsurfaceTint = AddPrefixIfNotExists("textures\\", txstObj.EnvironmentMaskOrSubsurfaceTint).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] EnvironmentMaskOrSubsurfaceTint: " + EnvironmentMaskOrSubsurfaceTint, 0);
                    slotsArray[5] = Marshal.StringToHGlobalUni(EnvironmentMaskOrSubsurfaceTint);
                }
                if (!txstObj.Multilayer.IsNullOrEmpty())
                {
                    var Multilayer = AddPrefixIfNotExists("textures\\", txstObj.Multilayer).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] Multilayer: " + Multilayer, 0);
                    slotsArray[6] = Marshal.StringToHGlobalUni(Multilayer);
                }
                if (!txstObj.BacklightMaskOrSpecular.IsNullOrEmpty())
                {
                    var BacklightMaskOrSpecular = AddPrefixIfNotExists("textures\\", txstObj.BacklightMaskOrSpecular).ToLower();
                    MessageHandler.Log("[GetTXSTSlots] [TXST Index: " + txstIndex + "] BacklightMaskOrSpecular: " + BacklightMaskOrSpecular, 0);
                    slotsArray[7] = Marshal.StringToHGlobalUni(BacklightMaskOrSpecular);
                }
                slotsArray[8] = IntPtr.Zero;
            }
            catch (Exception)
            {
                MessageHandler.Log("Failed to get TXST slots for record " + GetRecordDesc(txstObj), 3);
                for (int i = 0; i < 9; i++)
                {
                    slotsArray[i] = IntPtr.Zero;
                }
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "CreateNewTXSTPatch", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void CreateNewTXSTPatch([DNNE.C99Type("const int")] int AltTexHandle, [DNNE.C99Type("const wchar_t**")] IntPtr* slots, [DNNE.C99Type("const char*")] IntPtr NewEDID, [DNNE.C99Type("const unsigned int")] uint NewFormID, [DNNE.C99Type("int*")] int* ResultTXSTId)
    {
        try
        {
            if (OutMod is null || Env is null)
            {
                throw new Exception("Initialize must be called before CreateNewTXSTPatch");
            }

            // Get EDID from C++
            string NewEDIDStr = Marshal.PtrToStringAnsi(NewEDID) ?? string.Empty;

            // Create a new TXST record at a specific formID
            var newFormKey = new FormKey(OutMod.ModKey, NewFormID);
            var newTXSTObj = new TextureSet(newFormKey, Env.GameRelease.ToSkyrimRelease());

            newTXSTObj.EditorID = NewEDIDStr;

            // Define slot actions for assigning texture set slots
            string? NewDiffuse = Marshal.PtrToStringUni(slots[0]);
            if (!NewDiffuse.IsNullOrEmpty())
            {
                var Diffuse = RemovePrefixIfExists("textures\\", NewDiffuse);
                newTXSTObj.Diffuse = Diffuse;
            }
            string? NewNormalOrGloss = Marshal.PtrToStringUni(slots[1]);
            if (!NewNormalOrGloss.IsNullOrEmpty())
            {
                var NormalOrGloss = RemovePrefixIfExists("textures\\", NewNormalOrGloss);
                newTXSTObj.NormalOrGloss = NormalOrGloss;
            }
            string? NewGlowOrDetailMap = Marshal.PtrToStringUni(slots[2]);
            if (!NewGlowOrDetailMap.IsNullOrEmpty())
            {
                var GlowOrDetailMap = RemovePrefixIfExists("textures\\", NewGlowOrDetailMap);
                newTXSTObj.GlowOrDetailMap = GlowOrDetailMap;
            }
            string? NewHeight = Marshal.PtrToStringUni(slots[3]);
            if (!NewHeight.IsNullOrEmpty())
            {
                var Height = RemovePrefixIfExists("textures\\", NewHeight);
                newTXSTObj.Height = Height;
            }
            string? NewEnvironment = Marshal.PtrToStringUni(slots[4]);
            if (!NewEnvironment.IsNullOrEmpty())
            {
                var Environment = RemovePrefixIfExists("textures\\", NewEnvironment);
                newTXSTObj.Environment = Environment;
            }
            string? NewEnvironmentMaskOrSubsurfaceTint = Marshal.PtrToStringUni(slots[5]);
            if (!NewEnvironmentMaskOrSubsurfaceTint.IsNullOrEmpty())
            {
                var EnvironmentMaskOrSubsurfaceTint = RemovePrefixIfExists("textures\\", NewEnvironmentMaskOrSubsurfaceTint);
                newTXSTObj.EnvironmentMaskOrSubsurfaceTint = EnvironmentMaskOrSubsurfaceTint;
            }
            string? NewMultilayer = Marshal.PtrToStringUni(slots[6]);
            if (!NewMultilayer.IsNullOrEmpty())
            {
                var Multilayer = RemovePrefixIfExists("textures\\", NewMultilayer);
                newTXSTObj.Multilayer = Multilayer;
            }
            string? NewBacklightMaskOrSpecular = Marshal.PtrToStringUni(slots[7]);
            if (!NewBacklightMaskOrSpecular.IsNullOrEmpty())
            {
                var BacklightMaskOrSpecular = RemovePrefixIfExists("textures\\", NewBacklightMaskOrSpecular);
                newTXSTObj.BacklightMaskOrSpecular = BacklightMaskOrSpecular;
            }

            OutMod.TextureSets.Add(newTXSTObj);
            TXSTObjs.Add(newTXSTObj);
            *ResultTXSTId = TXSTObjs.Count - 1;

            // set maxFormID if needed
            if (NewFormID > maxFormID)
            {
                maxFormID = NewFormID;
            }
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "SetModelAltTex", CallConvs = [typeof(CallConvCdecl)])]
    public static void SetModelAltTex([DNNE.C99Type("const int")] int AltTexHandle, [DNNE.C99Type("const int")] int TXSTHandle)
    {
        try
        {
            var AltTexObj = GetAltTexFromHandle(AltTexHandle);
            if (AltTexObj.Item1 is null)
            {
                throw new Exception("Alt Texture handle not found");
            }

            if (AltTexObj.Item1.NewTexture.FormKey == TXSTObjs[TXSTHandle].FormKey)
            {
                // The formkey is already the same so we do nothing
                return;
            }

            AltTexObj.Item1.NewTexture.SetTo(TXSTObjs[TXSTHandle]);
            ModifiedModeledRecords.Add(AltTexObj.Item3);
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "Set3DIndex", CallConvs = [typeof(CallConvCdecl)])]
    public static void Set3DIndex([DNNE.C99Type("const int")] int AltTexHandle, [DNNE.C99Type("const int")] int NewIndex)
    {
        try
        {
            var AltTexObj = GetAltTexFromHandle(AltTexHandle);
            if (AltTexObj.Item1 is null)
            {
                throw new Exception("Alt Texture handle not found");
            }

            AltTexObj.Item1.Index = NewIndex;
            ModifiedModeledRecords.Add(AltTexObj.Item3);
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "SetModelRecNIF", CallConvs = [typeof(CallConvCdecl)])]
    public static void SetModelRecNIF([DNNE.C99Type("const int")] int AltTexHandle, [DNNE.C99Type("const wchar_t*")] IntPtr NIFPathPtr)
    {
        try
        {
            var AltTexObj = GetAltTexFromHandle(AltTexHandle);
            var ModelRec = AltTexObj.Item2;

            if (ModelRec is null)
            {
                throw new Exception("Model Record does not have a model");
            }

            var NIFPath = Marshal.PtrToStringUni(NIFPathPtr);
            if (NIFPath is null)
            {
                throw new Exception("NIF Path is null");
            }

            // remove "meshes" from beginning of NIFPath if exists
            NIFPath = RemovePrefixIfExists("meshes\\", NIFPath);

            // Check if NIFPath is different from ModelRec ignore case
            if (NIFPath.Equals(ModelRec.File, StringComparison.OrdinalIgnoreCase))
            {
                MessageHandler.Log("[SetModelRecNIF] [Alt Tex Index: " + AltTexHandle + "] [NIF Path: " + NIFPath + "] NIF Path is the same as the current one", 0);
                return;
            }

            ModelRec.File = NIFPath;
            ModifiedModeledRecords.Add(AltTexObj.Item3);
            MessageHandler.Log("[SetModelRecNIF] [Alt Tex Index: " + AltTexHandle + "] [NIF Path: " + NIFPath + "]", 0);
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetTXSTFormID", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetTXSTFormID([DNNE.C99Type("const int")] int TXSTHandle, [DNNE.C99Type("unsigned int*")] uint* FormID, [DNNE.C99Type("wchar_t**")] IntPtr* PluginName, [DNNE.C99Type("wchar_t**")] IntPtr* WinningPluginName)
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before GetTXSTFormID");
            }

            var txstObj = TXSTObjs[TXSTHandle];
            var PluginNameStr = txstObj.FormKey.ModKey.FileName;

            try
            {
                if (txstObj.ToLink().TryResolveSimpleContext(Env.LinkCache, out var context))
                {
                    *WinningPluginName = Marshal.StringToHGlobalUni(context.ModKey.FileName);
                }
                else
                {
                    *WinningPluginName = Marshal.StringToHGlobalUni(PluginNameStr);
                }
            }
            catch (Exception)
            {
                *WinningPluginName = Marshal.StringToHGlobalUni("UNKNOWN");
            }

            *PluginName = Marshal.StringToHGlobalUni(PluginNameStr);
            var FormIDStr = txstObj.FormKey.ID;
            *FormID = FormIDStr;
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetModelRecFormID", CallConvs = [typeof(CallConvCdecl)])]
    public unsafe static void GetModelRecFormID([DNNE.C99Type("const int")] int ModelRecHandle, [DNNE.C99Type("unsigned int*")] uint* FormID, [DNNE.C99Type("wchar_t**")] IntPtr* PluginName, [DNNE.C99Type("wchar_t**")] IntPtr* WinningPluginName)
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before GetModelRecFormID");
            }

            var modelRecObj = ModelOriginals[ModelRecHandle];
            var PluginNameStr = modelRecObj.FormKey.ModKey.FileName;

            try
            {
                if (modelRecObj.ToLink().TryResolveSimpleContext(Env.LinkCache, out var context))
                {
                    *WinningPluginName = Marshal.StringToHGlobalUni(context.ModKey.FileName);
                }
                else
                {
                    *WinningPluginName = Marshal.StringToHGlobalUni(PluginNameStr);
                }
            }
            catch (Exception)
            {
                *WinningPluginName = Marshal.StringToHGlobalUni("UNKNOWN");
            }

            *PluginName = Marshal.StringToHGlobalUni(PluginNameStr);
            var FormIDStr = modelRecObj.FormKey.ID;
            *FormID = FormIDStr;
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetAltTexFormID", CallConvs = [typeof(CallConvCdecl)])]
    public unsafe static void GetAltTexFormID([DNNE.C99Type("const int")] int AltTexHandle, [DNNE.C99Type("unsigned int*")] uint* FormID, [DNNE.C99Type("wchar_t**")] IntPtr* PluginName, [DNNE.C99Type("wchar_t**")] IntPtr* WinningPluginName)
    {
        try
        {
            if (Env is null)
            {
                throw new Exception("Initialize must be called before GetAltTexFormID");
            }

            var altTexObj = GetAltTexFromHandle(AltTexHandle);
            var modelRecObj = ModelOriginals[altTexObj.Item3];
            var PluginNameStr = modelRecObj.FormKey.ModKey.FileName;

            try
            {
                if (modelRecObj.ToLink().TryResolveSimpleContext(Env.LinkCache, out var context))
                {
                    *WinningPluginName = Marshal.StringToHGlobalUni(context.ModKey.FileName);
                }
                else
                {
                    *WinningPluginName = Marshal.StringToHGlobalUni(PluginNameStr);
                }
            }
            catch (Exception)
            {
                *WinningPluginName = Marshal.StringToHGlobalUni("UNKNOWN");
            }

            *PluginName = Marshal.StringToHGlobalUni(PluginNameStr);
            var FormIDStr = modelRecObj.FormKey.ID;
            *FormID = FormIDStr;
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "GetModelRecHandleFromAltTexHandle", CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe void GetModelRecHandleFromAltTexHandle([DNNE.C99Type("const int")] int AltTexHandle, [DNNE.C99Type("int*")] int* ModelRecHandle)
    {
        // TODO move this to getmatchingtxstrecords
        try
        {
            *ModelRecHandle = AltTexRefs[AltTexHandle].Item3;
        }
        catch (Exception ex)
        {
            ExceptionHandler.SetLastException(ex);
        }
    }

    // Helpers

    private static (AlternateTexture?, IModel?, int) GetAltTexFromHandle(int AltTexHandle)
    {
        var oldAltTex = AltTexRefs[AltTexHandle].Item1;
        var oldType = AltTexRefs[AltTexHandle].Item4;
        var ModeledRecordId = AltTexRefs[AltTexHandle].Item2;

        var ModeledRecord = ModelCopies[ModeledRecordId];

        // loop through alternate textures to find the one to replace
        var modelElems = GetModelElems(ModeledRecord);
        for (int i = 0; i < modelElems.Count; i++)
        {
            var modelRec = modelElems[i];
            if (modelRec.Item1.AlternateTextures is null)
            {
                continue;
            }

            if (oldType != modelRec.Item2)
            {
                // useful for records with multiple MODL records like ARMO
                continue;
            }

            for (int j = 0; j < modelRec.Item1.AlternateTextures.Count; j++)
            {
                var alternateTexture = modelRec.Item1.AlternateTextures[j];
                if (alternateTexture.Name == oldAltTex.Name &&
                    alternateTexture.Index == oldAltTex.Index &&
                    alternateTexture.NewTexture.FormKey.ID.ToString() == oldAltTex.NewTexture.FormKey.ID.ToString())
                {
                    // Found the one to update
                    var EditableModelObj = GetModelElems(ModelCopiesEditable[ModeledRecordId])[i];
                    var EditableAltTexObj = EditableModelObj.Item1.AlternateTextures?[j];
                    return (EditableAltTexObj, EditableModelObj.Item1, ModeledRecordId);
                }
            }
        }

        return (null, null, ModeledRecordId);
    }

    private static string GetRecordDesc(IMajorRecordGetter rec)
    {
        return rec.FormKey.ModKey.FileName + " / " + rec.FormKey.ID.ToString("X6");
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
