#include "GUI/LauncherWindow.hpp"

#include "GUI/DialogModifiableListCtrl.hpp"
#include "GUI/DialogRecTypeSelector.hpp"
#include "GUI/DialogSettings.hpp"
#include "GUI/DialogTextureMapListCtrl.hpp"
#include "GUI/PGMessageBox.hpp"
#include "PGConfig.hpp"
#include "PGLocale.hpp"
#include "PGModManager.hpp"
#include "PGPatcherGlobals.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
#include "common/BethesdaGame.hpp"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/msw/colour.h>
#include <wx/statline.h>
#include <wx/toplevel.h>
#include <wx/wx.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// class LauncherWindow
LauncherWindow::LauncherWindow(PGConfig& pgc)
    : wxDialog(nullptr,
               wxID_ANY,
               wxString::Format(PGTr("launcher.title",
                                     "PGPatcher %s Launcher"),
                                PG_FULL_VERSION),
               wxDefaultPosition,
               wxSize(MIN_WIDTH,
                      DEFAULT_HEIGHT),
               wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX | wxRESIZE_BORDER)
    , m_pgc(pgc)
    , m_gameLocationLocked(false)
    , m_gameLocationLockedByInstallLocation(false)
{
    // Calculate the scrollbar width (if visible)
    static const int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);

    // Main sizer
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create a horizontal sizer for left and right columns
    auto* columnsSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left/Right sizers
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->SetMinSize(wxSize(LEFTSIZER_MIN_SIZE, -1));
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    //
    // Left Panel
    //

    //
    // Game
    //
    auto* gameSizer = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.game.title", "Game"));

    // Game Location
    auto* gameLocationLabel = new wxStaticText(this, wxID_ANY, PGTr("launcher.game.location.label", "Location"));
    m_gameLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_gameLocationTextbox->SetToolTip(
        PGTr("launcher.game.location.tooltip", "Path to the game folder (NOT the data folder)"));
    m_gameLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onGameLocationChange, this);
    m_gameLocationBrowseButton = new wxButton(this, wxID_ANY, PGTr("common.browse", "Browse"));
    m_gameLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseGameLocation, this);

    auto* gameLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    gameLocationSizer->Add(m_gameLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    gameLocationSizer->Add(m_gameLocationBrowseButton, 0, wxALL, BORDER_SIZE);

    gameSizer->Add(gameLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    gameSizer->Add(gameLocationSizer, 0, wxEXPAND);

    // Game Type
    auto* gameTypeLabel = new wxStaticText(this, wxID_ANY, PGTr("launcher.game.type.label", "Type"));
    gameSizer->Add(gameTypeLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    bool isFirst = true;
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        auto* radio = new wxRadioButton(this,
                                        wxID_ANY,
                                        BethesdaGame::getStrFromGameType(gameType),
                                        wxDefaultPosition,
                                        wxDefaultSize,
                                        isFirst ? wxRB_GROUP : 0);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onGameTypeChange, this);
        isFirst = false;
        m_gameTypeRadios[gameType] = radio;
        gameSizer->Add(radio, 0, wxALL, BORDER_SIZE);
    }

    leftSizer->Add(gameSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Mod Manager
    //
    auto* modManagerSizer = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.modManager.title", "Mod Manager"));

    isFirst = true;
    for (const auto& mmType : PGModManager::getModManagerTypes()) {
        auto mmString = wxString(PGModManager::getStrFromModManagerType(mmType));
        if (mmType == PGModManager::ModManagerType::NONE) {
            mmString += PGTr("launcher.modManager.noneSuffix", " (No Conflict Resolution)");
        }

        auto* radio
            = new wxRadioButton(this, wxID_ANY, mmString, wxDefaultPosition, wxDefaultSize, isFirst ? wxRB_GROUP : 0);
        isFirst = false;
        m_modManagerRadios[mmType] = radio;
        modManagerSizer->Add(radio, 0, wxALL, BORDER_SIZE);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onModManagerChange, this);
    }

    leftSizer->Add(modManagerSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // MO2-specific controls (initially hidden)
    m_mo2OptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.mo2Options.title", "MO2 Options"));

    auto* mo2InstanceLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* mo2InstanceLocationLabel
        = new wxStaticText(this, wxID_ANY, PGTr("launcher.mo2Options.instanceLocation.label", "Instance Location"));

    m_mo2InstanceLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_mo2InstanceLocationTextbox->SetToolTip(
        PGTr("launcher.mo2Options.instanceLocation.tooltip",
             "Path to the MO2 instance folder (Folder Icon > Open Instance folder in MO2)"));
    m_mo2InstanceLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onMO2InstanceLocationChange, this);

    m_mo2InstanceBrowseButton = new wxButton(this, wxID_ANY, PGTr("common.browse", "Browse"));
    m_mo2InstanceBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseMO2InstanceLocation, this);

    mo2InstanceLocationSizer->Add(m_mo2InstanceLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    mo2InstanceLocationSizer->Add(m_mo2InstanceBrowseButton, 0, wxALL, BORDER_SIZE);

    // Add the label and dropdown to MO2 options sizer
    m_mo2OptionsSizer->Add(mo2InstanceLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    m_mo2OptionsSizer->Add(mo2InstanceLocationSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 0);

    // Add MO2 options to leftSizer but hide it initially
    modManagerSizer->Add(m_mo2OptionsSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Output
    //
    auto* outputSizer = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.output.title", "Output"));

    auto* outputLocationLabel = new wxStaticText(
        this,
        wxID_ANY,
        PGTr("launcher.output.location.help",
             "Location"));
    outputLocationLabel->Wrap(LEFTSIZER_WRAP_SIZE);
    m_outputLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_outputLocationTextbox->SetToolTip(
        PGTr("launcher.output.location.tooltip",
             "Path to the output folder - This folder should be used EXCLUSIVELY for PGPatcher, recommended to be a mod folder"));
    m_outputLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onOutputLocationChange, this);

    auto* outputLocationBrowseButton = new wxButton(this, wxID_ANY, PGTr("common.browse", "Browse"));
    outputLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseOutputLocation, this);

    auto* outputLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    outputLocationSizer->Add(m_outputLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    outputLocationSizer->Add(outputLocationBrowseButton, 0, wxALL, BORDER_SIZE);

    outputSizer->Add(outputLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    outputSizer->Add(outputLocationSizer, 0, wxEXPAND);

    m_outputZipCheckbox = new wxCheckBox(
        this, wxID_ANY, PGTr("launcher.output.zip.label", "Zip Output (Keep disabled if outputting to a mod folder)"));
    m_outputZipCheckbox->SetToolTip(PGTr("launcher.output.zip.tooltip", "Zip the output folder after processing"));
    m_outputZipCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onOutputZipChange, this);

    outputSizer->Add(m_outputZipCheckbox, 0, wxALL, BORDER_SIZE);

    // Create horizontal sizer for label + combo
    auto* langSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add label
    auto* langLabel = new wxStaticText(this, wxID_ANY, PGTr("launcher.output.pluginLang.label", "Plugin Language"));
    langSizer->Add(langLabel, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BORDER_SIZE);

    wxArrayString pluginLangs;
    for (const auto& lang : PGPlugin::getAvailablePluginLangStrs()) {
        pluginLangs.Add(lang);
    }
    m_outputPluginLangCombo = new wxComboBox(this,
                                             wxID_ANY,
                                             PGTr("launcher.output.pluginLang.placeholder", "Language"),
                                             wxDefaultPosition,
                                             wxDefaultSize,
                                             pluginLangs,
                                             wxCB_READONLY);
    m_outputPluginLangCombo->Bind(wxEVT_COMBOBOX, &LauncherWindow::onOutputPluginLangChange, this);
    m_outputPluginLangCombo->SetToolTip(
        PGTr("launcher.output.pluginLang.tooltip",
             "Language of embedded strings in output plugin. If a translation for this language is not available for "
             "a record, the default will be used which is usually English."));
    langSizer->Add(m_outputPluginLangCombo, 1, wxEXPAND | wxLEFT, BORDER_SIZE);

    outputSizer->Add(langSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    leftSizer->Add(outputSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Right Panel
    //

    //
    // Pre-Patchers
    //
    auto* prePatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.prePatchers.title", "Pre-Patchers"));

    m_prePatcherFixMeshLightingCheckbox = new wxCheckBox(
        this, wxID_ANY, PGTr("launcher.prePatchers.fixMeshLighting.label", "Fix Mesh Lighting (ENB Only)"));
    m_prePatcherFixMeshLightingCheckbox->SetToolTip(
        PGTr("launcher.prePatchers.fixMeshLighting.tooltip", "Fixes glowing meshes (For ENB users only!)"));
    m_prePatcherFixMeshLightingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPrePatcherFixMeshLightingChange, this);
    prePatcherSizer->Add(m_prePatcherFixMeshLightingCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(prePatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Shader Patchers
    //
    auto* shaderPatcherSizer
        = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.shaderPatchers.title", "Shader Patchers"));

    m_shaderPatcherParallaxCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.shaderPatchers.parallax.label", "Parallax"));
    m_shaderPatcherParallaxCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherParallaxChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherParallaxCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherComplexMaterialCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.shaderPatchers.complexMaterial.label", "Complex Material"));
    m_shaderPatcherComplexMaterialCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherComplexMaterialChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherComplexMaterialCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherTruePBRCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.shaderPatchers.truePBR.label", "TruePBR (CS Only)"));
    m_shaderPatcherTruePBRCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherTruePBRChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherTruePBRCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(shaderPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Shader Transforms
    //
    auto* shaderTransformSizer
        = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.shaderTransforms.title", "Shader Transforms"));

    m_shaderTransformParallaxToCMCheckbox = new wxCheckBox(
        this, wxID_ANY, PGTr("launcher.shaderTransforms.parallaxToCM.label", "Upgrade Parallax to Complex Material"));
    m_shaderTransformParallaxToCMCheckbox->SetToolTip(
        PGTr("launcher.shaderTransforms.parallaxToCM.tooltip",
             "Upgrades parallax textures and meshes to complex material when required for compatibility (highly "
             "recommended)"));
    m_shaderTransformParallaxToCMCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderTransformParallaxToCMChange, this);
    shaderTransformSizer->Add(m_shaderTransformParallaxToCMCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(shaderTransformSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Post-Patchers
    //
    auto* postPatcherSizer
        = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.postPatchers.title", "Post-Patchers"));

    m_postPatcherRestoreDefaultShadersCheckbox = new wxCheckBox(
        this,
        wxID_ANY,
        PGTr("launcher.postPatchers.disablePrePatchedMaterials.label", "Disable Pre-Patched Materials"));
    m_postPatcherRestoreDefaultShadersCheckbox->SetToolTip(
        PGTr("launcher.postPatchers.disablePrePatchedMaterials.tooltip",
             "Restores shaders to default if parallax or complex material textures are missing (highly recommended, "
             "replaces auto parallax functionality)"));
    m_postPatcherRestoreDefaultShadersCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherRestoreDefaultShadersChange, this);
    postPatcherSizer->Add(m_postPatcherRestoreDefaultShadersCheckbox, 0, wxALL, BORDER_SIZE);

    m_postPatcherFixSSSCheckbox = new wxCheckBox(
        this, wxID_ANY, PGTr("launcher.postPatchers.fixSSS.label", "Fix Vanilla Subsurface Scattering"));
    m_postPatcherFixSSSCheckbox->SetToolTip(
        PGTr("launcher.postPatchers.fixSSS.tooltip", "Fixes subsurface scattering in meshes, especially foliage"));
    m_postPatcherFixSSSCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherFixSSSChange, this);
    postPatcherSizer->Add(m_postPatcherFixSSSCheckbox, 0, wxALL, BORDER_SIZE);

    m_postPatcherHairFlowMapCheckbox = new wxCheckBox(
        this, wxID_ANY, PGTr("launcher.postPatchers.hairFlowMap.label", "Add Hair Flow Map (CS Only)"));
    m_postPatcherHairFlowMapCheckbox->SetToolTip(
        PGTr("launcher.postPatchers.hairFlowMap.tooltip",
             "Adds flow maps to texture sets for those that match the normal texture"));
    m_postPatcherHairFlowMapCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherHairFlowMapChange, this);
    postPatcherSizer->Add(m_postPatcherHairFlowMapCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(postPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Global Patchers
    //
    // auto* globalPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Global Patchers");
    // rightSizer->Add(globalPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Processing and RUN buttons
    //

    // Restore defaults button
    auto* restoreDefaultsButton
        = new wxButton(this, wxID_ANY, PGTr("launcher.buttons.restoreDefaults", "Restore Defaults"));
    wxFont restoreDefaultsButtonFont = restoreDefaultsButton->GetFont();
    restoreDefaultsButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    restoreDefaultsButton->SetFont(restoreDefaultsButtonFont);
    restoreDefaultsButton->Bind(wxEVT_BUTTON, &LauncherWindow::onRestoreDefaultsButtonPressed, this);
    rightSizer->Add(restoreDefaultsButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Load config button
    m_loadConfigButton = new wxButton(this, wxID_ANY, PGTr("launcher.buttons.loadConfig", "Load Config"));
    wxFont loadConfigButtonFont = m_loadConfigButton->GetFont();
    loadConfigButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    m_loadConfigButton->SetFont(loadConfigButtonFont);
    m_loadConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onLoadConfigButtonPressed, this);
    rightSizer->Add(m_loadConfigButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Save config button
    m_saveConfigButton = new wxButton(this, wxID_ANY, PGTr("launcher.buttons.saveConfig", "Save Config"));
    wxFont saveConfigButtonFont = m_saveConfigButton->GetFont();
    saveConfigButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    m_saveConfigButton->SetFont(saveConfigButtonFont);
    m_saveConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onSaveConfigButtonPressed, this);
    rightSizer->Add(m_saveConfigButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Add a horizontal line
    auto* separatorLine = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    rightSizer->Add(separatorLine, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // cancel button on the right side
    auto* cancelButton = new wxButton(this, wxID_CANCEL, PGTr("common.cancel", "Cancel"));
    wxFont cancelButtonFont = cancelButton->GetFont();
    cancelButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    cancelButton->SetFont(cancelButtonFont);
    cancelButton->Bind(wxEVT_BUTTON, &LauncherWindow::onCancelButtonPressed, this);
    rightSizer->Add(cancelButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Start Patching button on the right side
    m_okButton = new wxButton(this, wxID_ANY, PGTr("launcher.buttons.startPatching", "Start Patching"));
    wxFont okButtonFont = m_okButton->GetFont();
    okButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    okButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_okButton->SetFont(okButtonFont);
    m_okButton->SetToolTip(PGTr("launcher.buttons.startPatchingTooltip",
                                "Generate the output from scratch (any previous output in the output location is "
                                "replaced)"));
    m_okButton->Bind(wxEVT_BUTTON, &LauncherWindow::onOkButtonPressed, this);
    Bind(wxEVT_CLOSE_WINDOW, &LauncherWindow::onClose, this);
    rightSizer->Add(m_okButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Update Output button below it (only enabled when the output location holds a previous output)
    m_updateOutputButton = new wxButton(this, wxID_ANY, PGTr("launcher.buttons.updateOutput", "Update Output"));
    wxFont updateOutputButtonFont = m_updateOutputButton->GetFont();
    updateOutputButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    m_updateOutputButton->SetFont(updateOutputButtonFont);
    m_updateOutputButton->SetToolTip(
        PGTr("launcher.buttons.updateOutputTooltip",
             "Update the previous output in the output location: only meshes whose inputs changed since that output "
             "was generated are patched again. Available when the output location contains a previous output and zip "
             "output is disabled."));
    m_updateOutputButton->Bind(wxEVT_BUTTON, &LauncherWindow::onUpdateOutputButtonPressed, this);
    rightSizer->Add(m_updateOutputButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Processing
    //
    m_processingOptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, PGTr("launcher.processing.title", "Processing"));

    auto* processingHelpText = new wxStaticText(
        this,
        wxID_ANY,
        PGTr("launcher.processing.help",
             "These options are used to customize output generation. Avoid changing these unless you know what you "
             "are doing."));
    processingHelpText->Wrap(LEFTSIZER_WRAP_SIZE);
    m_processingOptionsSizer->Add(processingHelpText, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    auto* processingOptionsHorizontalSizer = new wxBoxSizer(wxHORIZONTAL);

    auto* processingButtonsSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnOpenDialogRecTypeSelector
        = new wxButton(this, wxID_ANY, PGTr("launcher.processing.allowedRecordTypes", "Allowed Record Types"));
    btnOpenDialogRecTypeSelector->Bind(wxEVT_BUTTON, &LauncherWindow::onSelectPluginTypesBtn, this);
    processingButtonsSizer->Add(btnOpenDialogRecTypeSelector, 0, wxALL | wxEXPAND, BORDER_SIZE);

    auto* btnOpenDialogMeshAllowlist
        = new wxButton(this, wxID_ANY, PGTr("launcher.processing.meshAllowlist", "Mesh Allowlist"));
    btnOpenDialogMeshAllowlist->Bind(wxEVT_BUTTON, &LauncherWindow::onMeshRulesAllowBtn, this);
    processingButtonsSizer->Add(btnOpenDialogMeshAllowlist, 0, wxALL | wxEXPAND, BORDER_SIZE);

    auto* btnOpenDialogMeshBlocklist
        = new wxButton(this, wxID_ANY, PGTr("launcher.processing.meshBlocklist", "Mesh Blocklist"));
    btnOpenDialogMeshBlocklist->Bind(wxEVT_BUTTON, &LauncherWindow::onMeshRulesBlockBtn, this);
    processingButtonsSizer->Add(btnOpenDialogMeshBlocklist, 0, wxALL | wxEXPAND, BORDER_SIZE);

    auto* btnOpenDialogTextureMaps
        = new wxButton(this, wxID_ANY, PGTr("launcher.processing.textureRules", "Texture Rules"));
    btnOpenDialogTextureMaps->Bind(wxEVT_BUTTON, &LauncherWindow::onTextureRulesTextureMapsBtn, this);
    processingButtonsSizer->Add(btnOpenDialogTextureMaps, 0, wxALL | wxEXPAND, BORDER_SIZE);

    processingOptionsHorizontalSizer->Add(processingButtonsSizer, 0, wxALL, 0);

    auto* processingCheckboxSizer = new wxBoxSizer(wxVERTICAL);

    m_processingMultithreadingCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.processing.multithreading.label", "Multithreading"));
    m_processingMultithreadingCheckbox->SetToolTip(
        PGTr("launcher.processing.multithreading.tooltip", "Speeds up runtime at the cost of using more resources"));
    m_processingMultithreadingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingMultithreadingChange, this);
    processingCheckboxSizer->Add(m_processingMultithreadingCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingEnableDevModeCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.processing.devMode.label", "Enable Mod Dev Mode"));
    m_processingEnableDevModeCheckbox->SetToolTip(
        PGTr("launcher.processing.devMode.tooltip",
             "Enables certain warnings to help those developing mods to work with PGPatcher"));
    m_processingEnableDevModeCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableDevModeChange, this);
    processingCheckboxSizer->Add(m_processingEnableDevModeCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingEnableDebugLoggingCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.processing.debugLogging.label", "Enable Debug Logging"));
    m_processingEnableDebugLoggingCheckbox->SetToolTip(
        PGTr("launcher.processing.debugLogging.tooltip", "Enables debug logging in the output log"));
    m_processingEnableDebugLoggingCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableDebugLoggingChange, this);
    processingCheckboxSizer->Add(m_processingEnableDebugLoggingCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingEnableTraceLoggingCheckbox
        = new wxCheckBox(this, wxID_ANY, PGTr("launcher.processing.traceLogging.label", "Enable Trace Logging"));
    m_processingEnableTraceLoggingCheckbox->SetToolTip(
        PGTr("launcher.processing.traceLogging.tooltip", "Enables trace logging in the output log (very verbose)"));
    m_processingEnableTraceLoggingCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableTraceLoggingChange, this);
    processingCheckboxSizer->Add(m_processingEnableTraceLoggingCheckbox, 0, wxALL, BORDER_SIZE);

    processingOptionsHorizontalSizer->Add(processingCheckboxSizer, 0, wxALL, BORDER_SIZE);

    m_processingOptionsSizer->Add(processingOptionsHorizontalSizer, 0, wxALL, 0);

    leftSizer->Add(m_processingOptionsSizer, 1, wxEXPAND | wxALL, BORDER_SIZE);

    // Add help ? button to the bottom right of the whole window that opens the wiki URL on click
    auto* helpButton = new wxButton(this, wxID_ANY, "?");
    wxFont helpButtonFont = helpButton->GetFont();
    helpButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    helpButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
    helpButton->SetFont(helpButtonFont);

    helpButton->SetToolTip(PGTr("launcher.helpButton.tooltip", "Open the PGPatcher wiki"));

    const wxSize helpBtnSize = wxSize(HELPBTN_SIZE, HELPBTN_SIZE);
    helpButton->SetMinSize(helpBtnSize);
    helpButton->SetMaxSize(helpBtnSize);

    helpButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) -> void {
        wxLaunchDefaultBrowser("https://github.com/hakasapl/PGPatcher/wiki");
    });

    // Settings (gear) button next to the help button
    auto* settingsButton = new wxButton(this, wxID_ANY, wxEmptyString);

    wxBitmapBundle settingsIconBundle;
    const filesystem::path settingsSVGPath = PGPatcherGlobals::getEXEPath() / "resources" / "settings.svg";
    if (filesystem::exists(settingsSVGPath)) {
        ifstream settingsSVGStream(settingsSVGPath);
        string settingsSVGData(istreambuf_iterator<char> { settingsSVGStream }, istreambuf_iterator<char> {});
        // the SVG fill is currentColor, which the wx SVG renderer cannot resolve, so substitute the theme color
        boost::replace_all(settingsSVGData, "currentColor", PGPatcherGlobals::isDarkMode() ? "#ffffff" : "#000000");
        settingsIconBundle = wxBitmapBundle::FromSVG(
            settingsSVGData.c_str(), wxSize(SETTINGSBTN_ICON_SIZE, SETTINGSBTN_ICON_SIZE));
    }
    if (settingsIconBundle.IsOk()) {
        settingsButton->SetBitmap(settingsIconBundle);
    } else {
        // fall back to the gear glyph if the SVG resource is unavailable
        settingsButton->SetLabel(wxString(wxUniChar(0x2699)));
        wxFont settingsButtonFont = settingsButton->GetFont();
        settingsButtonFont.SetPointSize(BUTTON_FONT_SIZE);
        settingsButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
        settingsButton->SetFont(settingsButtonFont);
    }

    settingsButton->SetToolTip(PGTr("launcher.settingsButton.tooltip", "Open PGPatcher settings"));
    settingsButton->SetMinSize(helpBtnSize);
    settingsButton->SetMaxSize(helpBtnSize);
    settingsButton->Bind(wxEVT_BUTTON, &LauncherWindow::onSettingsButtonPressed, this);

    auto* bottomButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    bottomButtonSizer->Add(helpButton, 0, wxRIGHT, BORDER_SIZE);
    bottomButtonSizer->Add(settingsButton, 0, 0, 0);

    rightSizer->AddStretchSpacer(1);
    rightSizer->Add(bottomButtonSizer, 0, wxALL | wxALIGN_LEFT, BORDER_SIZE);

    //
    // Finalize
    //

    columnsSizer->Add(leftSizer, 1, wxEXPAND | wxALL, 0);
    columnsSizer->Add(rightSizer, 0, wxEXPAND | wxALL, 0);

    mainSizer->Add(columnsSizer, 1, wxEXPAND | wxALL, BORDER_SIZE);

    SetSizerAndFit(mainSizer);
    const auto curSize = GetSize();
    SetSize(MIN_WIDTH, curSize.GetY());
    SetSizeHints(wxSize(MIN_WIDTH, curSize.GetY()), wxSize(-1, curSize.GetY()));

    Bind(wxEVT_INIT_DIALOG, &LauncherWindow::onInitDialog, this);
}

void LauncherWindow::onInitDialog(wxInitDialogEvent& event)
{
    loadConfig();

    // Trigger the updateDeps event to update the dependencies
    updateDisabledElements();
    setGamePathBasedOnExe();

    // Call the base class's event handler if needed
    event.Skip();
}

void LauncherWindow::loadConfig()
{
    // This is where we populate existing params
    const auto initParams = m_pgc.getParams();

    // Game
    if (!m_gameLocationLocked) {
        m_gameLocationTextbox->SetValue(initParams.Game.dir.wstring());
    }
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (gameType == initParams.Game.type) {
            m_gameTypeRadios[gameType]->SetValue(true);
        }
    }

    // Mod Manager
    for (const auto& mmType : PGModManager::getModManagerTypes()) {
        if (mmType == initParams.ModManager.type) {
            m_modManagerRadios[mmType]->SetValue(true);

            // Show MO2 options only if MO2 is selected
            if (mmType == PGModManager::ModManagerType::MODORGANIZER2) {
                m_mo2InstanceLocationTextbox->Enable(true);
                m_mo2InstanceBrowseButton->Enable(true);
            } else {
                m_mo2InstanceLocationTextbox->Enable(false);
                m_mo2InstanceBrowseButton->Enable(false);
            }
        }
    }

    // MO2-specific options
    m_mo2InstanceLocationTextbox->SetValue(initParams.ModManager.mo2InstanceDir.wstring());

    // Manually trigger the onMO2InstanceLocationChange to populate the listbox
    wxCommandEvent changeEvent(wxEVT_TEXT, m_mo2InstanceLocationTextbox->GetId());
    onMO2InstanceLocationChange(changeEvent); // Call the handler directly

    // Output
    m_outputLocationTextbox->SetValue(initParams.Output.dir.wstring());
    m_outputZipCheckbox->SetValue(initParams.Output.zip);
    m_outputPluginLangCombo->SetStringSelection(PGPlugin::getStringFromPluginLang(initParams.Output.pluginLang));

    // Processing
    m_processingMultithreadingCheckbox->SetValue(initParams.Processing.multithread);
    m_processingEnableDevModeCheckbox->SetValue(initParams.Processing.enableModDevMode);
    m_processingEnableDebugLoggingCheckbox->SetValue(initParams.Processing.enableDebugLogging);
    m_processingEnableTraceLoggingCheckbox->SetValue(initParams.Processing.enableTraceLogging);
    m_meshRulesAllowListState = initParams.Processing.allowList;
    m_meshRulesBlockListState = initParams.Processing.blockList;
    m_textureRulesTextureMapsState = initParams.Processing.textureMaps;
    m_DialogRecTypeSelectorState = initParams.Processing.allowedModelRecordTypes;

    // Pre-Patchers
    m_prePatcherFixMeshLightingCheckbox->SetValue(initParams.PrePatcher.fixMeshLighting);

    // Shader Patchers
    m_shaderPatcherParallaxCheckbox->SetValue(initParams.ShaderPatcher.parallax);
    m_shaderPatcherComplexMaterialCheckbox->SetValue(initParams.ShaderPatcher.complexMaterial);
    m_shaderPatcherTruePBRCheckbox->SetValue(initParams.ShaderPatcher.truePBR);

    // Shader Transforms
    m_shaderTransformParallaxToCMCheckbox->SetValue(initParams.ShaderTransforms.parallaxToCM);

    // Post-Patchers
    m_postPatcherRestoreDefaultShadersCheckbox->SetValue(initParams.PostPatcher.disablePrePatchedMaterials);
    m_postPatcherFixSSSCheckbox->SetValue(initParams.PostPatcher.fixSSS);
    m_postPatcherHairFlowMapCheckbox->SetValue(initParams.PostPatcher.hairFlowMap);

    // Global Patchers
}

// Component event handlers

void LauncherWindow::onGameLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onGameTypeChange([[maybe_unused]] wxCommandEvent& event)
{
    if (m_gameLocationLocked) {
        updateDisabledElements();
        return;
    }

    const auto initParams = m_pgc.getParams();

    // update the game location textbox from bethesdagame
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios[gameType]->GetValue()) {
            if (initParams.Game.type == gameType) {
                m_gameLocationTextbox->SetValue(initParams.Game.dir.wstring());
            } else {
                m_gameLocationTextbox->SetValue(BethesdaGame::findGamePathFromSteam(gameType).wstring());
            }

            setGamePathBasedOnExe();
            return;
        }
    }

    updateDisabledElements();
}

void LauncherWindow::onModManagerChange([[maybe_unused]] wxCommandEvent& event)
{
    // Show MO2 options only if the MO2 radio button is selected
    const bool isMO2Selected
        = (event.GetEventObject() == m_modManagerRadios[PGModManager::ModManagerType::MODORGANIZER2]);
    m_mo2InstanceLocationTextbox->Enable(isMO2Selected);
    m_mo2InstanceBrowseButton->Enable(isMO2Selected);

    updateMO2Items();

    Layout(); // Refresh layout to apply visibility changes
    Fit();

    updateDisabledElements();
    setGamePathBasedOnExe();
}

void LauncherWindow::onOutputLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onOutputZipChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onOutputPluginLangChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onProcessingMultithreadingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingEnableDevModeChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingEnableDebugLoggingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingEnableTraceLoggingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPrePatcherFixMeshLightingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherParallaxChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onShaderPatcherComplexMaterialChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherTruePBRChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onShaderTransformParallaxToCMChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPostPatcherRestoreDefaultShadersChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPostPatcherFixSSSChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onPostPatcherHairFlowMapChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onMeshRulesAllowBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogModifiableListCtrl dialog(
        this,
        PGTr("dialogs.meshAllowlist.title", "Mesh Rules Allowlist"),
        PGTr("dialogs.meshAllowlist.description",
             "If any rules exist here, only meshes matching them will be patched. Enter path to mesh like "
             "\"meshes/armor/helmet.nif\" or use wildcards (* is the wildcard) to allowlist entire "
             "folders/files. Right click to add/remove entries."));
    dialog.populateList(m_meshRulesAllowListState);
    if (dialog.ShowModal() == wxID_OK) {
        m_meshRulesAllowListState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onMeshRulesBlockBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogModifiableListCtrl dialog(
        this,
        PGTr("dialogs.meshBlocklist.title", "Mesh Rules Blocklist"),
        PGTr("dialogs.meshBlocklist.description",
             "Any meshes matching rules here will not be patched. Enter path to mesh like "
             "\"meshes/armor/helmet.nif\" or use wildcards (* is the wildcard) to blocklist entire "
             "folders/files. Right click to add/remove entries."));
    dialog.populateList(m_meshRulesBlockListState);
    if (dialog.ShowModal() == wxID_OK) {
        m_meshRulesBlockListState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onTextureRulesTextureMapsBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogTextureMapListCtrl dialog(
        this,
        PGTr("dialogs.textureRules.title", "Texture Rules"),
        PGTr("dialogs.textureRules.description",
             "Use this to tell PGPatcher what type of texture something is if the auto detection is wrong (very "
             "rare). Enter the full path to the texture like \"textures/armor/helmet.dds\" and select the type of "
             "texture. Wildcards are NOT supported here. A texture can be ignored by setting it to \"unknown\". "
             "Right click to add/remove entries."));
    dialog.populateList(m_textureRulesTextureMapsState);
    if (dialog.ShowModal() == wxID_OK) {
        m_textureRulesTextureMapsState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onSelectPluginTypesBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogRecTypeSelector selectorDialog(this, PGTr("dialogs.recTypeSelector.title", "Allowed Record Types"));
    selectorDialog.populateList(m_DialogRecTypeSelectorState);
    if (selectorDialog.ShowModal() == wxID_OK) {
        m_DialogRecTypeSelectorState = selectorDialog.getSelectedRecordTypes();
        updateDisabledElements();
    }
}

void LauncherWindow::getParams(PGConfig::PGParams& params) const
{
    // Game
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios.at(gameType)->GetValue()) {
            params.Game.type = gameType;
            break;
        }
    }
    params.Game.dir = m_gameLocationTextbox->GetValue().ToStdWstring();

    // Mod Manager
    for (const auto& mmType : PGModManager::getModManagerTypes()) {
        if (m_modManagerRadios.at(mmType)->GetValue()) {
            params.ModManager.type = mmType;
            break;
        }
    }
    params.ModManager.mo2InstanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Output
    params.Output.dir = m_outputLocationTextbox->GetValue().ToStdWstring();
    params.Output.zip = m_outputZipCheckbox->GetValue();
    params.Output.pluginLang
        = PGPlugin::getPluginLangFromString(m_outputPluginLangCombo->GetStringSelection().ToStdString());

    // Processing
    params.Processing.multithread = m_processingMultithreadingCheckbox->GetValue();
    params.Processing.enableModDevMode = m_processingEnableDevModeCheckbox->GetValue();
    params.Processing.enableDebugLogging = m_processingEnableDebugLoggingCheckbox->GetValue();
    params.Processing.enableTraceLogging = m_processingEnableTraceLoggingCheckbox->GetValue();
    params.Processing.allowList = m_meshRulesAllowListState;
    params.Processing.blockList = m_meshRulesBlockListState;
    params.Processing.textureMaps = m_textureRulesTextureMapsState;
    params.Processing.allowedModelRecordTypes = m_DialogRecTypeSelectorState;

    // Pre-Patchers
    params.PrePatcher.fixMeshLighting = m_prePatcherFixMeshLightingCheckbox->GetValue();

    // Shader Patchers
    params.ShaderPatcher.parallax = m_shaderPatcherParallaxCheckbox->GetValue();
    params.ShaderPatcher.complexMaterial = m_shaderPatcherComplexMaterialCheckbox->GetValue();
    params.ShaderPatcher.truePBR = m_shaderPatcherTruePBRCheckbox->GetValue();

    // Shader Transforms
    params.ShaderTransforms.parallaxToCM = m_shaderTransformParallaxToCMCheckbox->GetValue();

    // Post-Patchers
    params.PostPatcher.disablePrePatchedMaterials = m_postPatcherRestoreDefaultShadersCheckbox->GetValue();
    params.PostPatcher.fixSSS = m_postPatcherFixSSSCheckbox->GetValue();
    params.PostPatcher.hairFlowMap = m_postPatcherHairFlowMapCheckbox->GetValue();

    // Global Patchers
}

void LauncherWindow::onBrowseGameLocation([[maybe_unused]] wxCommandEvent& event)
{
    if (m_gameLocationLocked) {
        return;
    }

    wxDirDialog dialog(
        this, PGTr("launcher.browse.gameLocation", "Select Game Location"), m_gameLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_gameLocationTextbox->SetValue(dialog.GetPath());
    }
}

void LauncherWindow::onBrowseMO2InstanceLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(this,
                       PGTr("launcher.browse.mo2InstanceLocation", "Select MO2 Instance Location"),
                       m_mo2InstanceLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_mo2InstanceLocationTextbox->SetValue(dialog.GetPath());
    }

    // Trigger the change event to update the profiles
    wxCommandEvent changeEvent(wxEVT_TEXT, m_mo2InstanceLocationTextbox->GetId());
    onMO2InstanceLocationChange(changeEvent); // Call the handler directly
}

void LauncherWindow::updateMO2Items()
{
    // check if MO2 is selected
    if (!m_modManagerRadios[PGModManager::ModManagerType::MODORGANIZER2]->GetValue()) {
        const bool shouldLock = m_gameLocationLockedByInstallLocation;
        m_gameLocationTextbox->Enable(!shouldLock);
        m_gameLocationBrowseButton->Enable(!shouldLock);
        m_gameLocationLocked = shouldLock;
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(true);
        }
        return;
    }

    const auto instanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Get game path
    const auto gamePathMO2 = PGModManager::getGamePathFromInstanceDir(instanceDir);
    const bool lockByMO2Path = !gamePathMO2.empty();
    if (lockByMO2Path) {
        // found the game path, set it to the game location textbox
        m_gameLocationTextbox->SetValue(gamePathMO2.wstring());
    }

    const bool shouldLock = m_gameLocationLockedByInstallLocation || lockByMO2Path;
    m_gameLocationTextbox->Enable(!shouldLock);
    m_gameLocationBrowseButton->Enable(!shouldLock);
    m_gameLocationLocked = shouldLock;

    // Get game type
    const auto gameTypeMO2 = PGModManager::getGameTypeFromInstanceDir(instanceDir);
    if (gameTypeMO2 != BethesdaGame::GameType::UNKNOWN) {
        m_gameTypeRadios[gameTypeMO2]->SetValue(true);
        // disable all radio buttons
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(false);
        }
    } else {
        // enable all radio buttons
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(true);
        }
    }
}

void LauncherWindow::onMO2InstanceLocationChange([[maybe_unused]] wxCommandEvent& event) { updateMO2Items(); }

void LauncherWindow::onBrowseOutputLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(
        this, PGTr("launcher.browse.outputLocation", "Select Output Location"), m_outputLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_outputLocationTextbox->SetValue(dialog.GetPath());
    }
}

void LauncherWindow::updateDisabledElements()
{
    PGConfig::PGParams curParams = m_pgc.getParams();
    getParams(curParams);

    // Upgrade parallax to CM rules
    if (curParams.ShaderTransforms.parallaxToCM) {
        // disable and check vanilla parallax patcher
        m_shaderPatcherParallaxCheckbox->SetValue(true);
        m_shaderPatcherParallaxCheckbox->Enable(false);

        // disable and check CM patcher
        m_shaderPatcherComplexMaterialCheckbox->SetValue(true);
        m_shaderPatcherComplexMaterialCheckbox->Enable(false);
    } else {
        m_shaderPatcherParallaxCheckbox->Enable(true);
        m_shaderPatcherComplexMaterialCheckbox->Enable(true);
    }

    // save button
    m_saveConfigButton->Enable(curParams != m_pgc.getParams());

    // update output button: only when the current output location holds a previous output that can be updated
    m_updateOutputButton->Enable(!curParams.Output.zip && PGRunCache::isUpdateAvailable(curParams.Output.dir));

    // logging checkboxes
    if (curParams.Processing.enableDebugLogging) {
        m_processingEnableTraceLoggingCheckbox->Enable(true);
    } else {
        m_processingEnableTraceLoggingCheckbox->SetValue(false);
        m_processingEnableTraceLoggingCheckbox->Enable(false);
    }
}

void LauncherWindow::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // All validation passed, proceed with OK actions
        m_updateRequested = false;
        EndModal(wxID_OK);
    }
}

void LauncherWindow::onUpdateOutputButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        m_updateRequested = true;
        EndModal(wxID_OK);
    }
}

auto LauncherWindow::isUpdateRequested() const -> bool { return m_updateRequested; }

void LauncherWindow::onCancelButtonPressed([[maybe_unused]] wxCommandEvent& event) { wxTheApp->Exit(); }

void LauncherWindow::onSaveConfigButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // Disable button
        updateDisabledElements();
    }
}

void LauncherWindow::onLoadConfigButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    const int response
        = PGMessageBox(PGTr("launcher.confirmLoadConfig.message",
                            "Are you sure you want to load the config from the file? This action will overwrite all "
                            "current unsaved settings."),
                       PGTr("launcher.confirmLoadConfig.title", "Confirm Load Config"),
                       wxYES_NO | wxICON_WARNING,
                       this);

    if (response != wxYES) {
        return;
    }

    // Load the config from the file
    loadConfig();

    updateDisabledElements();
}

void LauncherWindow::onRestoreDefaultsButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    // Show a confirmation dialog
    const int response
        = PGMessageBox(PGTr("launcher.confirmRestoreDefaults.message",
                            "Are you sure you want to restore the default settings? This action cannot be undone."),
                       PGTr("launcher.confirmRestoreDefaults.title", "Confirm Restore Defaults"),
                       wxYES_NO | wxICON_WARNING,
                       this);

    if (response != wxYES) {
        return;
    }

    // Reset the config to the default
    m_pgc.setParams(PGConfig::getDefaultParams());

    loadConfig();

    updateDisabledElements();
}

void LauncherWindow::onSettingsButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    DialogSettings dialog(this, m_pgc);
    dialog.ShowModal();

    if (dialog.languageChanged() || dialog.themeChanged()) {
        // Preserve the current (possibly unsaved) UI state in memory so the rebuilt launcher shows the same values
        auto curParams = m_pgc.getParams();
        getParams(curParams);
        m_pgc.setParams(curParams);

        EndModal(RESULT_RELAUNCH);
    }
}

auto LauncherWindow::saveConfig() -> bool
{
    vector<string> errors;
    PGConfig::PGParams params = m_pgc.getParams();
    getParams(params);

    // Validate the parameters
    if (!PGConfig::validateParams(params, errors)) {
        PGMessageBox(boost::algorithm::join(errors, "\n"), PGTr("common.errors", "Errors"), wxOK | wxICON_ERROR, this);
        return false;
    }

    m_pgc.setParams(params);
    m_pgc.saveUserConfig();
    return true;
}

void LauncherWindow::onClose([[maybe_unused]] wxCloseEvent& event) { wxTheApp->Exit(); }

void LauncherWindow::setGamePathBasedOnExe()
{
    const auto exePath = PGPatcherGlobals::getEXEPath();
    if (exePath.empty()) {
        m_gameLocationLockedByInstallLocation = false;
        return;
    }

    auto curParams = m_pgc.getParams();
    getParams(curParams);
    const auto curGameType = curParams.Game.type;

    const auto gamePath = exePath.parent_path().parent_path();
    m_gameLocationLockedByInstallLocation = BethesdaGame::isGamePathValid(gamePath, curGameType);

    const auto curModManagerType = curParams.ModManager.type;
    if (curModManagerType == PGModManager::ModManagerType::MODORGANIZER2) {
        // Keep MO2 path selection behavior, but preserve install-location lock precedence.
        updateMO2Items();
        return;
    }

    if (m_gameLocationLockedByInstallLocation) {
        m_gameLocationTextbox->SetValue(gamePath.wstring());

        // disable textbox and browse button
        m_gameLocationTextbox->Enable(false);
        m_gameLocationBrowseButton->Enable(false);
        m_gameLocationLocked = true;
    } else {
        // enable textbox and browse button
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
        m_gameLocationLocked = false;
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
