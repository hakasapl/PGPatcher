#include "GUI/LauncherWindow.hpp"

#include "ModManagerDirectory.hpp"
#include "ParallaxGenConfig.hpp"
#include "ParallaxGenHandlers.hpp"
#include "ParallaxGenPlugin.hpp"

#include <boost/algorithm/string/join.hpp>

#include <filesystem>
#include <wx/arrstr.h>
#include <wx/listbase.h>
#include <wx/msw/combobox.h>
#include <wx/statline.h>

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// class LauncherWindow
LauncherWindow::LauncherWindow(ParallaxGenConfig& pgc, std::filesystem::path cacheDir)
    : wxDialog(nullptr, wxID_ANY, "PGPatcher " + string(PG_VERSION) + " Launcher", wxDefaultPosition,
          wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT), wxDEFAULT_DIALOG_STYLE)
    , m_cacheDir(std::move(cacheDir))
    , m_pgc(pgc)
    , m_textureMapTypeCombo(nullptr)
    , m_advancedOptionsSizer(new wxBoxSizer(wxVERTICAL))
{
    // Calculate the scrollbar width (if visible)
    static const int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);

    // Main sizer
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create a horizontal sizer for left and right columns
    auto* columnsSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left/Right sizers
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->SetMinSize(wxSize(DEFAULT_WIDTH, -1));
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    //
    // Left Panel
    //

    //
    // Game
    //
    auto* gameSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Game");

    // Game Location
    auto* gameLocationLabel = new wxStaticText(this, wxID_ANY, "Location");
    m_gameLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_gameLocationTextbox->SetToolTip("Path to the game folder (NOT the data folder)");
    m_gameLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onGameLocationChange, this);
    m_gameLocationBrowseButton = new wxButton(this, wxID_ANY, "Browse");
    m_gameLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseGameLocation, this);

    auto* gameLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    gameLocationSizer->Add(m_gameLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    gameLocationSizer->Add(m_gameLocationBrowseButton, 0, wxALL, BORDER_SIZE);

    gameSizer->Add(gameLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    gameSizer->Add(gameLocationSizer, 0, wxEXPAND);

    // Game Type
    auto* gameTypeLabel = new wxStaticText(this, wxID_ANY, "Type");
    gameSizer->Add(gameTypeLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    bool isFirst = true;
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        auto* radio = new wxRadioButton(this, wxID_ANY, BethesdaGame::getStrFromGameType(gameType), wxDefaultPosition,
            wxDefaultSize, isFirst ? wxRB_GROUP : 0);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onGameTypeChange, this);
        isFirst = false;
        m_gameTypeRadios[gameType] = radio;
        gameSizer->Add(radio, 0, wxALL, BORDER_SIZE);
    }

    leftSizer->Add(gameSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Mod Manager
    //
    auto* modManagerSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Conflict Resolution Mod Manager");

    isFirst = true;
    for (const auto& mmType : ModManagerDirectory::getModManagerTypes()) {
        auto* radio = new wxRadioButton(this, wxID_ANY, ModManagerDirectory::getStrFromModManagerType(mmType),
            wxDefaultPosition, wxDefaultSize, isFirst ? wxRB_GROUP : 0);
        isFirst = false;
        m_modManagerRadios[mmType] = radio;
        modManagerSizer->Add(radio, 0, wxALL, BORDER_SIZE);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onModManagerChange, this);
    }

    leftSizer->Add(modManagerSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // MO2-specific controls (initially hidden)
    m_mo2OptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "MO2 Options");

    auto* mo2InstanceLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* mo2InstanceLocationLabel = new wxStaticText(this, wxID_ANY, "Instance Location");

    m_mo2InstanceLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_mo2InstanceLocationTextbox->SetToolTip(
        "Path to the MO2 instance folder (Folder Icon > Open Instnace folder in MO2)");
    m_mo2InstanceLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onMO2InstanceLocationChange, this);

    auto* mo2InstanceBrowseButton = new wxButton(this, wxID_ANY, "Browse");
    mo2InstanceBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseMO2InstanceLocation, this);

    mo2InstanceLocationSizer->Add(m_mo2InstanceLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    mo2InstanceLocationSizer->Add(mo2InstanceBrowseButton, 0, wxALL, BORDER_SIZE);

    // Add the label and dropdown to MO2 options sizer
    m_mo2OptionsSizer->Add(mo2InstanceLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    m_mo2OptionsSizer->Add(mo2InstanceLocationSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 0);

    // Add MO2 options to leftSizer but hide it initially
    modManagerSizer->Add(m_mo2OptionsSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Output
    //
    auto* outputSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Output");

    auto* outputLocationLabel = new wxStaticText(this, wxID_ANY, "Location");
    m_outputLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_outputLocationTextbox->SetToolTip(
        "Path to the output folder - This folder should be used EXCLUSIVELY for ParallaxGen. Don't set it to your data "
        "directory or any other folder that contains mods.");
    m_outputLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onOutputLocationChange, this);

    auto* outputLocationBrowseButton = new wxButton(this, wxID_ANY, "Browse");
    outputLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseOutputLocation, this);

    auto* outputLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    outputLocationSizer->Add(m_outputLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    outputLocationSizer->Add(outputLocationBrowseButton, 0, wxALL, BORDER_SIZE);

    outputSizer->Add(outputLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    outputSizer->Add(outputLocationSizer, 0, wxEXPAND);

    m_outputZipCheckbox = new wxCheckBox(this, wxID_ANY, "Zip Output");
    m_outputZipCheckbox->SetToolTip("Zip the output folder after processing");

    outputSizer->Add(m_outputZipCheckbox, 0, wxALL, BORDER_SIZE);
    leftSizer->Add(outputSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Advanced
    //
    auto* advancedSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Advanced");
    m_advancedOptionsCheckbox = new wxCheckBox(this, wxID_ANY, "Show Advanced Options");
    m_advancedOptionsCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onAdvancedOptionsChange, this);
    advancedSizer->Add(m_advancedOptionsCheckbox, 0, wxALL, BORDER_SIZE);

    leftSizer->Add(advancedSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Right Panel
    //

    //
    // Pre-Patchers
    //
    auto* prePatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Pre-Patchers");

    m_prePatcherDisableMLPCheckbox = new wxCheckBox(this, wxID_ANY, "Disable Multi-Layer Parallax");
    m_prePatcherDisableMLPCheckbox->SetToolTip("Disables Multi-Layer Parallax in all meshes (Usually not recommended)");
    m_prePatcherDisableMLPCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPrePatcherDisableMLPChange, this);
    prePatcherSizer->Add(m_prePatcherDisableMLPCheckbox, 0, wxALL, BORDER_SIZE);

    m_prePatcherFixMeshLightingCheckbox = new wxCheckBox(this, wxID_ANY, "Fix Mesh Lighting (ENB Only)");
    m_prePatcherFixMeshLightingCheckbox->SetToolTip("Fixes glowing meshes (For ENB users only!)");
    m_prePatcherFixMeshLightingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPrePatcherFixMeshLightingChange, this);
    prePatcherSizer->Add(m_prePatcherFixMeshLightingCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(prePatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Shader Patchers
    //
    auto* shaderPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Shader Patchers");

    m_shaderPatcherParallaxCheckbox = new wxCheckBox(this, wxID_ANY, "Parallax");
    m_shaderPatcherParallaxCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherParallaxChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherParallaxCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherComplexMaterialCheckbox = new wxCheckBox(this, wxID_ANY, "Complex Material");
    m_shaderPatcherComplexMaterialCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherComplexMaterialChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherComplexMaterialCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherComplexMaterialOptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Complex Material Options");
    auto* shaderPatcherComplexMaterialDynCubemapBlocklistLabel
        = new wxStaticText(this, wxID_ANY, "Dynamic Cubemap Blocklist");
    m_shaderPatcherComplexMaterialOptionsSizer->Add(
        shaderPatcherComplexMaterialDynCubemapBlocklistLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    m_shaderPatcherComplexMaterialDynCubemapBlocklist = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->AppendColumn("DynCubemap Blocklist", wxLIST_FORMAT_LEFT);
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->SetColumnWidth(
        0, m_shaderPatcherComplexMaterialDynCubemapBlocklist->GetClientSize().GetWidth() - scrollbarWidth);
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->Bind(
        wxEVT_LIST_END_LABEL_EDIT, &LauncherWindow::onShaderPatcherComplexMaterialDynCubemapBlocklistChange, this);
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->Bind(
        wxEVT_LIST_ITEM_ACTIVATED, &LauncherWindow::onListItemActivated, this);
    m_shaderPatcherComplexMaterialOptionsSizer->Add(
        m_shaderPatcherComplexMaterialDynCubemapBlocklist, 0, wxEXPAND | wxALL, BORDER_SIZE);

    shaderPatcherSizer->Add(m_shaderPatcherComplexMaterialOptionsSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_shaderPatcherTruePBRCheckbox = new wxCheckBox(this, wxID_ANY, "True PBR (CS Only)");
    m_shaderPatcherTruePBRCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherTruePBRChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherTruePBRCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherTruePBROptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "True PBR Options");
    m_shaderPatcherTruePBRCheckPathsCheckbox = new wxCheckBox(this, wxID_ANY, "Check Paths");
    m_shaderPatcherTruePBRCheckPathsCheckbox->SetToolTip("Do not patch JSONs that create paths that do not exist");
    m_shaderPatcherTruePBRCheckPathsCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherTruePBRCheckPathsChange, this);
    m_shaderPatcherTruePBROptionsSizer->Add(m_shaderPatcherTruePBRCheckPathsCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox = new wxCheckBox(this, wxID_ANY, "Print Non-Existent Paths");
    m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox->SetToolTip(
        "Print any paths generated from JSONs that do not exist");
    m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherTruePBRPrintNonExistentPathsChange, this);
    m_shaderPatcherTruePBROptionsSizer->Add(m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox, 0, wxALL, BORDER_SIZE);

    shaderPatcherSizer->Add(m_shaderPatcherTruePBROptionsSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    rightSizer->Add(shaderPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Shader Transforms
    //
    auto* shaderTransformSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Shader Transforms");

    m_shaderTransformParallaxToCMCheckbox = new wxCheckBox(this, wxID_ANY, "Upgrade Parallax to Complex Material");
    m_shaderTransformParallaxToCMCheckbox->SetToolTip("Upgrages any parallax textures and meshes to complex material "
                                                      "by moving the height map to the alpha channel of "
                                                      "the environment mask (highly recommended)");
    m_shaderTransformParallaxToCMCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderTransformParallaxToCMChange, this);
    shaderTransformSizer->Add(m_shaderTransformParallaxToCMCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(shaderTransformSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Post-Patchers
    //
    auto* postPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Post-Patchers");

    m_postPatcherFixSSSCheckbox = new wxCheckBox(this, wxID_ANY, "Fix Vanilla Subsurface Scattering (Experimental)");
    m_postPatcherFixSSSCheckbox->SetToolTip("Fixes subsurface scattering in meshes");
    m_postPatcherFixSSSCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherFixSSSChange, this);
    postPatcherSizer->Add(m_postPatcherFixSSSCheckbox, 0, wxALL, BORDER_SIZE);

    m_postPatcherHairFlowMapCheckbox = new wxCheckBox(this, wxID_ANY, "Add Hair Flow Map (CS Only)");
    m_postPatcherHairFlowMapCheckbox->SetToolTip("Adds a hair flow map to shapes with a matching normal");
    m_postPatcherHairFlowMapCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherHairFlowMapChange, this);
    postPatcherSizer->Add(m_postPatcherHairFlowMapCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(postPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Global Patchers
    //
    auto* globalPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Global Patchers");
    m_globalPatcherFixEffectLightingCSCheckbox = new wxCheckBox(this, wxID_ANY, "Fix Effect Lighting (CS Only)");
    m_globalPatcherFixEffectLightingCSCheckbox->SetToolTip("Fixes effect lighting in meshes (For CS users only!)");
    m_globalPatcherFixEffectLightingCSCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onGlobalPatcherFixEffectLightingCSChange, this);
    globalPatcherSizer->Add(m_globalPatcherFixEffectLightingCSCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(globalPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Restore defaults button
    auto* restoreDefaultsButton = new wxButton(this, wxID_ANY, "Restore Defaults");
    wxFont restoreDefaultsButtonFont = restoreDefaultsButton->GetFont();
    restoreDefaultsButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    restoreDefaultsButton->SetFont(restoreDefaultsButtonFont);
    restoreDefaultsButton->Bind(wxEVT_BUTTON, &LauncherWindow::onRestoreDefaultsButtonPressed, this);

    rightSizer->Add(restoreDefaultsButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Load config button
    m_loadConfigButton = new wxButton(this, wxID_ANY, "Load Config");
    wxFont loadConfigButtonFont = m_loadConfigButton->GetFont();
    loadConfigButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    m_loadConfigButton->SetFont(loadConfigButtonFont);
    m_loadConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onLoadConfigButtonPressed, this);

    rightSizer->Add(m_loadConfigButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Save config button
    m_saveConfigButton = new wxButton(this, wxID_ANY, "Save Config");
    wxFont saveConfigButtonFont = m_saveConfigButton->GetFont();
    saveConfigButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    m_saveConfigButton->SetFont(saveConfigButtonFont);
    m_saveConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onSaveConfigButtonPressed, this);

    rightSizer->Add(m_saveConfigButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Add a horizontal line
    auto* separatorLine = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    rightSizer->Add(separatorLine, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Start Patching button on the right side
    m_okButton = new wxButton(this, wxID_OK, "Start Patching");

    // Set font size for the "start patching" button
    wxFont okButtonFont = m_okButton->GetFont();
    okButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    m_okButton->SetFont(okButtonFont);
    m_okButton->Bind(wxEVT_BUTTON, &LauncherWindow::onOkButtonPressed, this);
    Bind(wxEVT_CLOSE_WINDOW, &LauncherWindow::onClose, this);

    rightSizer->Add(m_okButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Advanced
    //

    //
    // Processing
    //
    m_processingOptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Processing Options");

    m_processingPluginPatchingCheckbox = new wxCheckBox(this, wxID_ANY, "Plugin Patching");
    m_processingPluginPatchingCheckbox->SetToolTip("Creates a 'ParallaxGen.esp' plugin in the output that patches TXST "
                                                   "records according to how NIFs were patched");
    m_processingPluginPatchingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingPluginPatchingChange, this);
    m_processingOptionsSizer->Add(m_processingPluginPatchingCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingPluginPatchingOptions = new wxStaticBoxSizer(wxVERTICAL, this, "Plugin Patching Options");
    m_processingPluginPatchingOptionsESMifyCheckbox = new wxCheckBox(this, wxID_ANY, "ESMify Plugin (Not Recommended)");
    m_processingPluginPatchingOptionsESMifyCheckbox->SetToolTip(
        "ESM flags the output plugin (don't check this if you don't know what you're doing)");
    m_processingPluginPatchingOptionsESMifyCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingPluginPatchingOptionsESMifyChange, this);
    m_processingPluginPatchingOptions->Add(m_processingPluginPatchingOptionsESMifyCheckbox, 0, wxALL, BORDER_SIZE);

    // Create horizontal sizer for label + combo
    auto* langSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add label
    auto* langLabel = new wxStaticText(this, wxID_ANY, "Plugin Language");
    langSizer->Add(langLabel, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BORDER_SIZE);

    wxArrayString pluginLangs;
    for (const auto& lang : ParallaxGenPlugin::getAvailablePluginLangStrs()) {
        pluginLangs.Add(lang);
    }
    m_processingPluginPatchingOptionsLangCombo
        = new wxComboBox(this, wxID_ANY, "Language", wxDefaultPosition, wxDefaultSize, pluginLangs, wxCB_READONLY);
    m_processingPluginPatchingOptionsLangCombo->Bind(
        wxEVT_COMBOBOX, &LauncherWindow::onProcessingPluginPatchingOptionsLangChange, this);
    m_processingPluginPatchingOptionsLangCombo->SetToolTip(
        "Language of embedded strings in output plugin. If a translation for this language is not available for a "
        "record, the default will be used which is usually English.");
    langSizer->Add(m_processingPluginPatchingOptionsLangCombo, 1, wxEXPAND | wxLEFT, BORDER_SIZE);

    m_processingPluginPatchingOptions->Add(langSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_processingOptionsSizer->Add(m_processingPluginPatchingOptions, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_processingMultithreadingCheckbox = new wxCheckBox(this, wxID_ANY, "Multithreading");
    m_processingMultithreadingCheckbox->SetToolTip("Speeds up runtime at the cost of using more resources");
    m_processingMultithreadingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingMultithreadingChange, this);
    m_processingOptionsSizer->Add(m_processingMultithreadingCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingBSACheckbox = new wxCheckBox(this, wxID_ANY, "Read BSAs");
    m_processingBSACheckbox->SetToolTip("Read meshes/textures from BSAs in addition to loose files");
    m_processingBSACheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingBSAChange, this);
    m_processingOptionsSizer->Add(m_processingBSACheckbox, 0, wxALL, BORDER_SIZE);

    m_enableDiagnosticsCheckbox = new wxCheckBox(this, wxID_ANY, "Enable Diagnostics");
    m_enableDiagnosticsCheckbox->SetToolTip(
        "Generates ParallaxGen_DIAG.json. Usually only on request from the developer");
    m_enableDiagnosticsCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onEnableDiagnosticsChange, this);
    m_processingOptionsSizer->Add(m_enableDiagnosticsCheckbox, 0, wxALL, BORDER_SIZE);

    leftSizer->Add(m_processingOptionsSizer, 1, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Mesh Rules
    //
    auto* meshRulesSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Mesh Rules");

    auto* meshRulesAllowListLabel = new wxStaticText(this, wxID_ANY, "Mesh Allow List");
    meshRulesSizer->Add(meshRulesAllowListLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    m_meshRulesAllowList = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
    m_meshRulesAllowList->AppendColumn("Mesh Allow List", wxLIST_FORMAT_LEFT);
    m_meshRulesAllowList->SetColumnWidth(0, m_meshRulesAllowList->GetClientSize().GetWidth() - scrollbarWidth);
    m_meshRulesAllowList->SetToolTip("If anything is in this list, only meshes matching items in this list will be "
                                     "patched. Wildcards are supported.");
    m_meshRulesAllowList->Bind(wxEVT_LIST_END_LABEL_EDIT, &LauncherWindow::onMeshRulesAllowListChange, this);
    m_meshRulesAllowList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &LauncherWindow::onListItemActivated, this);
    meshRulesSizer->Add(m_meshRulesAllowList, 0, wxEXPAND | wxALL, BORDER_SIZE);

    auto* meshRulesBlockListLabel = new wxStaticText(this, wxID_ANY, "Mesh Block List");
    meshRulesSizer->Add(meshRulesBlockListLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    m_meshRulesBlockList = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
    m_meshRulesBlockList->AppendColumn("Mesh Block List", wxLIST_FORMAT_LEFT);
    m_meshRulesBlockList->SetColumnWidth(0, m_meshRulesBlockList->GetClientSize().GetWidth() - scrollbarWidth);
    m_meshRulesBlockList->SetToolTip(
        "Any matches in this list will not be patched. This is checked after allowlist. Wildcards are supported.");
    m_meshRulesBlockList->Bind(wxEVT_LIST_END_LABEL_EDIT, &LauncherWindow::onMeshRulesBlockListChange, this);
    m_meshRulesBlockList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &LauncherWindow::onListItemActivated, this);
    meshRulesSizer->Add(m_meshRulesBlockList, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_advancedOptionsSizer->Add(meshRulesSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Texture Rules
    //
    auto* textureRulesSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Texture Rules");

    auto* textureRulesMapsLabel = new wxStaticText(this, wxID_ANY, "Manual Texture Maps");
    textureRulesSizer->Add(textureRulesMapsLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    m_textureRulesMaps = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
    m_textureRulesMaps->AppendColumn("Texture Maps", wxLIST_FORMAT_LEFT);
    m_textureRulesMaps->AppendColumn("Type", wxLIST_FORMAT_LEFT);
    m_textureRulesMaps->SetColumnWidth(0, (m_textureRulesMaps->GetClientSize().GetWidth() - scrollbarWidth) / 2);
    m_textureRulesMaps->SetColumnWidth(1, (m_textureRulesMaps->GetClientSize().GetWidth() - scrollbarWidth) / 2);
    m_textureRulesMaps->SetToolTip(
        "Use this to manually specify what type of texture a texture is. This is useful for textures that don't follow "
        "the standard naming conventions. Set to unknown for it to be skipped.");
    m_textureRulesMaps->Bind(wxEVT_LIST_END_LABEL_EDIT, &LauncherWindow::onTextureRulesMapsChange, this);
    m_textureRulesMaps->Bind(wxEVT_LEFT_DCLICK, &LauncherWindow::onTextureRulesMapsChangeStart, this);
    textureRulesSizer->Add(m_textureRulesMaps, 0, wxEXPAND | wxALL, BORDER_SIZE);

    auto* textureRulesVanillaBSAListLabel = new wxStaticText(this, wxID_ANY, "Vanilla BSA List");
    textureRulesSizer->Add(textureRulesVanillaBSAListLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    m_textureRulesVanillaBSAList = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
    m_textureRulesVanillaBSAList->AppendColumn("Vanilla BSA List", wxLIST_FORMAT_LEFT);
    m_textureRulesVanillaBSAList->SetColumnWidth(
        0, m_textureRulesVanillaBSAList->GetClientSize().GetWidth() - scrollbarWidth);
    m_textureRulesVanillaBSAList->SetToolTip(
        "Define vanilla or vanilla-like BSAs. Files from these BSAs will only be considered vanilla types.");
    m_textureRulesVanillaBSAList->Bind(
        wxEVT_LIST_END_LABEL_EDIT, &LauncherWindow::onTextureRulesVanillaBSAListChange, this);
    m_textureRulesVanillaBSAList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &LauncherWindow::onListItemActivated, this);
    textureRulesSizer->Add(m_textureRulesVanillaBSAList, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_advancedOptionsSizer->Add(textureRulesSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Finalize
    //
    m_advancedOptionsSizer->Show(false); // Initially hidden
    m_shaderPatcherComplexMaterialOptionsSizer->Show(false); // Initially hidden
    m_shaderPatcherTruePBROptionsSizer->Show(false); // Initially hidden
    m_processingOptionsSizer->Show(false); // Initially hidden
    m_processingPluginPatchingOptions->Show(false); // Initially hidden

    columnsSizer->Add(m_advancedOptionsSizer, 0, wxEXPAND | wxALL, 0);
    columnsSizer->Add(leftSizer, 1, wxEXPAND | wxALL, 0);
    columnsSizer->Add(rightSizer, 0, wxEXPAND | wxALL, 0);

    mainSizer->Add(columnsSizer, 1, wxEXPAND | wxALL, BORDER_SIZE);

    SetSizerAndFit(mainSizer);
    SetSizeHints(this->GetSize());

    Bind(wxEVT_INIT_DIALOG, &LauncherWindow::onInitDialog, this);
}

void LauncherWindow::onInitDialog(wxInitDialogEvent& event)
{
    loadConfig();

    // Trigger the updateDeps event to update the dependencies
    updateDisabledElements();

    // Call the base class's event handler if needed
    event.Skip();
}

void LauncherWindow::loadConfig()
{
    // This is where we populate existing params
    const auto initParams = m_pgc.getParams();

    // Game
    m_gameLocationTextbox->SetValue(initParams.Game.dir.wstring());
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (gameType == initParams.Game.type) {
            m_gameTypeRadios[gameType]->SetValue(true);
        }
    }

    // Mod Manager
    for (const auto& mmType : ModManagerDirectory::getModManagerTypes()) {
        if (mmType == initParams.ModManager.type) {
            m_modManagerRadios[mmType]->SetValue(true);

            // Show MO2 options only if MO2 is selected
            if (mmType == ModManagerDirectory::ModManagerType::MODORGANIZER2) {
                m_mo2OptionsSizer->Show(true);
            } else {
                m_mo2OptionsSizer->Show(false);
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

    // Advanced
    m_advancedOptionsCheckbox->SetValue(initParams.advanced);

    // Processing
    m_processingPluginPatchingCheckbox->SetValue(initParams.Processing.pluginPatching);
    m_processingPluginPatchingOptionsESMifyCheckbox->SetValue(initParams.Processing.pluginESMify);
    m_processingPluginPatchingOptionsLangCombo->SetStringSelection(
        ParallaxGenPlugin::getStringFromPluginLang(initParams.Processing.pluginLang));
    m_processingMultithreadingCheckbox->SetValue(initParams.Processing.multithread);
    m_processingBSACheckbox->SetValue(initParams.Processing.bsa);
    m_enableDiagnosticsCheckbox->SetValue(initParams.Processing.diagnostics);

    // Pre-Patchers
    m_prePatcherDisableMLPCheckbox->SetValue(initParams.PrePatcher.disableMLP);
    m_prePatcherFixMeshLightingCheckbox->SetValue(initParams.PrePatcher.fixMeshLighting);

    // Shader Patchers
    m_shaderPatcherParallaxCheckbox->SetValue(initParams.ShaderPatcher.parallax);
    m_shaderPatcherComplexMaterialCheckbox->SetValue(initParams.ShaderPatcher.complexMaterial);
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->DeleteAllItems();
    for (const auto& dynCubemap : initParams.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist) {
        m_shaderPatcherComplexMaterialDynCubemapBlocklist->InsertItem(
            m_shaderPatcherComplexMaterialDynCubemapBlocklist->GetItemCount(), dynCubemap);
    }
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->InsertItem(
        m_shaderPatcherComplexMaterialDynCubemapBlocklist->GetItemCount(), ""); // Add empty line
    m_shaderPatcherComplexMaterialDynCubemapBlocklist->SetToolTip(
        "Textures or meshes matching elements of this list will not have dynamic cubemap applied with complex "
        "material. "
        "Wildcards are supported.");

    m_shaderPatcherTruePBRCheckbox->SetValue(initParams.ShaderPatcher.truePBR);
    m_shaderPatcherTruePBRCheckPathsCheckbox->SetValue(initParams.ShaderPatcher.ShaderPatcherTruePBR.checkPaths);
    m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox->SetValue(
        initParams.ShaderPatcher.ShaderPatcherTruePBR.printNonExistentPaths);

    // Shader Transforms
    m_shaderTransformParallaxToCMCheckbox->SetValue(initParams.ShaderTransforms.parallaxToCM);

    // Post-Patchers
    m_postPatcherFixSSSCheckbox->SetValue(initParams.PostPatcher.fixSSS);
    m_postPatcherHairFlowMapCheckbox->SetValue(initParams.PostPatcher.hairFlowMap);

    // Global Patchers
    m_globalPatcherFixEffectLightingCSCheckbox->SetValue(initParams.GlobalPatcher.fixEffectLightingCS);

    // Mesh Rules
    m_meshRulesAllowList->DeleteAllItems();
    for (const auto& meshRule : initParams.MeshRules.allowList) {
        m_meshRulesAllowList->InsertItem(m_meshRulesAllowList->GetItemCount(), meshRule);
    }
    m_meshRulesAllowList->InsertItem(m_meshRulesAllowList->GetItemCount(), ""); // Add empty line

    m_meshRulesBlockList->DeleteAllItems();
    for (const auto& meshRule : initParams.MeshRules.blockList) {
        m_meshRulesBlockList->InsertItem(m_meshRulesBlockList->GetItemCount(), meshRule);
    }
    m_meshRulesBlockList->InsertItem(m_meshRulesBlockList->GetItemCount(), ""); // Add empty line

    // Texture Rules
    m_textureRulesMaps->DeleteAllItems();
    for (const auto& textureRule : initParams.TextureRules.textureMaps) {
        const auto newIndex = m_textureRulesMaps->InsertItem(m_textureRulesMaps->GetItemCount(), textureRule.first);
        m_textureRulesMaps->SetItem(newIndex, 1, NIFUtil::getStrFromTexType(textureRule.second));
    }
    m_textureRulesMaps->InsertItem(m_textureRulesMaps->GetItemCount(), ""); // Add empty line

    m_textureRulesVanillaBSAList->DeleteAllItems();
    for (const auto& vanillaBSA : initParams.TextureRules.vanillaBSAList) {
        m_textureRulesVanillaBSAList->InsertItem(m_textureRulesVanillaBSAList->GetItemCount(), vanillaBSA);
    }
    m_textureRulesVanillaBSAList->InsertItem(m_textureRulesVanillaBSAList->GetItemCount(), ""); // Add empty line

    updateAdvanced();
}

// Component event handlers

void LauncherWindow::onGameLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onGameTypeChange([[maybe_unused]] wxCommandEvent& event)
{
    const auto initParams = m_pgc.getParams();

    // update the game location textbox from bethesdagame
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios[gameType]->GetValue()) {
            if (initParams.Game.type == gameType) {
                m_gameLocationTextbox->SetValue(initParams.Game.dir.wstring());
                return;
            }

            m_gameLocationTextbox->SetValue(BethesdaGame::findGamePathFromSteam(gameType).wstring());
            return;
        }
    }

    updateDisabledElements();
}

void LauncherWindow::onModManagerChange([[maybe_unused]] wxCommandEvent& event)
{
    // Show MO2 options only if the MO2 radio button is selected
    const bool isMO2Selected
        = (event.GetEventObject() == m_modManagerRadios[ModManagerDirectory::ModManagerType::MODORGANIZER2]);
    m_mo2OptionsSizer->Show(isMO2Selected);

    updateMO2Items();

    Layout(); // Refresh layout to apply visibility changes
    Fit();

    updateDisabledElements();
}

void LauncherWindow::onOutputLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onOutputZipChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onAdvancedOptionsChange([[maybe_unused]] wxCommandEvent& event)
{
    updateAdvanced();

    updateDisabledElements();
}

void LauncherWindow::updateAdvanced()
{
    const bool advancedVisible = m_advancedOptionsCheckbox->GetValue();

    m_advancedOptionsSizer->Show(advancedVisible);
    m_processingOptionsSizer->Show(advancedVisible);

    if (m_shaderPatcherComplexMaterialCheckbox->GetValue()) {
        m_shaderPatcherComplexMaterialOptionsSizer->Show(advancedVisible);
    }

    if (m_shaderPatcherTruePBRCheckbox->GetValue()) {
        m_shaderPatcherTruePBROptionsSizer->Show(advancedVisible);
    }

    Layout(); // Refresh layout to apply visibility changes
    Fit();
}

void LauncherWindow::onProcessingPluginPatchingChange([[maybe_unused]] wxCommandEvent& event)
{
    if (m_processingPluginPatchingCheckbox->GetValue()) {
        m_processingPluginPatchingOptions->Show(m_processingPluginPatchingCheckbox->GetValue());
        Layout(); // Refresh layout to apply visibility changes
        Fit();
    } else {
        m_processingPluginPatchingOptions->Show(false);
        Layout(); // Refresh layout to apply visibility changes
        Fit();
    }

    updateDisabledElements();
}

void LauncherWindow::onProcessingPluginPatchingOptionsESMifyChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingPluginPatchingOptionsLangChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingMultithreadingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onEnableDiagnosticsChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onProcessingBSAChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onPrePatcherDisableMLPChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onPrePatcherFixMeshLightingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onGlobalPatcherFixEffectLightingCSChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherParallaxChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onShaderPatcherComplexMaterialChange([[maybe_unused]] wxCommandEvent& event)
{
    if (m_shaderPatcherComplexMaterialCheckbox->GetValue() && m_advancedOptionsCheckbox->GetValue()) {
        m_shaderPatcherComplexMaterialOptionsSizer->Show(m_shaderPatcherComplexMaterialCheckbox->GetValue());
        Layout(); // Refresh layout to apply visibility changes
        Fit();
    } else {
        m_shaderPatcherComplexMaterialOptionsSizer->Show(false);
        Layout(); // Refresh layout to apply visibility changes
        Fit();
    }

    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherComplexMaterialDynCubemapBlocklistChange([[maybe_unused]] wxListEvent& event)
{
    onListEdit(event);

    this->CallAfter([this]() { updateDisabledElements(); });
}

void LauncherWindow::onShaderPatcherTruePBRChange([[maybe_unused]] wxCommandEvent& event)
{
    if (m_shaderPatcherTruePBRCheckbox->GetValue() && m_advancedOptionsCheckbox->GetValue()) {
        m_shaderPatcherTruePBROptionsSizer->Show(m_shaderPatcherTruePBRCheckbox->GetValue());
        Layout(); // Refresh layout to apply visibility changes
        Fit();
    } else {
        m_shaderPatcherTruePBROptionsSizer->Show(false);
        Layout(); // Refresh layout to apply visibility changes
        Fit();
    }

    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherTruePBRCheckPathsChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherTruePBRPrintNonExistentPathsChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderTransformParallaxToCMChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPostPatcherFixSSSChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onPostPatcherHairFlowMapChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onMeshRulesAllowListChange(wxListEvent& event)
{
    onListEdit(event);

    this->CallAfter([this]() { updateDisabledElements(); });
}

void LauncherWindow::onMeshRulesBlockListChange(wxListEvent& event)
{
    onListEdit(event);

    this->CallAfter([this]() { updateDisabledElements(); });
}

void LauncherWindow::onTextureRulesMapsChange([[maybe_unused]] wxListEvent& event)
{
    onListEdit(event);

    this->CallAfter([this]() { updateDisabledElements(); });
}

void LauncherWindow::onTextureRulesMapsChangeStart([[maybe_unused]] wxMouseEvent& event)
{
    static const auto possibleTexTypes = NIFUtil::getTexTypesStr();
    // convert to wxArrayStr
    wxArrayString wxPossibleTexTypes;
    for (const auto& texType : possibleTexTypes) {
        wxPossibleTexTypes.Add(texType);
    }

    const wxPoint pos = event.GetPosition();
    int flags = 0;
    const long item = m_textureRulesMaps->HitTest(pos, flags);

    if (item != wxNOT_FOUND && ((flags & wxLIST_HITTEST_ONITEM) != 0)) {
        const int column = getColumnAtPosition(pos, item);
        if (column == 0) {
            // Start editing the first column
            m_textureRulesMaps->EditLabel(item);
        } else if (column == 1) {
            // Create dropdown for the second column
            wxRect rect;
            m_textureRulesMaps->GetSubItemRect(item, column, rect);

            m_textureMapTypeCombo = new wxComboBox(m_textureRulesMaps, wxID_ANY, "", rect.GetTopLeft(), rect.GetSize(),
                wxPossibleTexTypes, wxCB_DROPDOWN | wxCB_READONLY);
            m_textureMapTypeCombo->SetFocus();
            m_textureMapTypeCombo->Popup();
            m_textureMapTypeCombo->Bind(wxEVT_COMBOBOX, [item, column, this](wxCommandEvent&) {
                m_textureRulesMaps->SetItem(item, column, m_textureMapTypeCombo->GetValue());
                m_textureMapTypeCombo->Show(false);

                // Check if it's the last row and add a new blank row
                if (item == m_textureRulesMaps->GetItemCount() - 1) {
                    m_textureRulesMaps->InsertItem(m_textureRulesMaps->GetItemCount(), "");
                }
            });

            m_textureMapTypeCombo->Bind(
                wxEVT_KILL_FOCUS, [this](wxFocusEvent&) { m_textureMapTypeCombo->Show(false); });
        }
    } else {
        event.Skip();
    }
}

auto LauncherWindow::getColumnAtPosition(const wxPoint& pos, long item) -> int
{
    wxRect rect;
    for (int col = 0; col < m_textureRulesMaps->GetColumnCount(); ++col) {
        m_textureRulesMaps->GetSubItemRect(item, col, rect);
        if (rect.Contains(pos)) {
            return col;
        }
    }
    return -1; // No column found
}

void LauncherWindow::onTextureRulesVanillaBSAListChange(wxListEvent& event)
{
    onListEdit(event);

    this->CallAfter([this]() { updateDisabledElements(); });
}

void LauncherWindow::getParams(ParallaxGenConfig::PGParams& params) const
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
    for (const auto& mmType : ModManagerDirectory::getModManagerTypes()) {
        if (m_modManagerRadios.at(mmType)->GetValue()) {
            params.ModManager.type = mmType;
            break;
        }
    }
    params.ModManager.mo2InstanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Output
    params.Output.dir = m_outputLocationTextbox->GetValue().ToStdWstring();
    params.Output.zip = m_outputZipCheckbox->GetValue();

    // Advanced
    params.advanced = m_advancedOptionsCheckbox->GetValue();

    // Processing
    params.Processing.pluginPatching = m_processingPluginPatchingCheckbox->GetValue();
    params.Processing.pluginESMify = m_processingPluginPatchingOptionsESMifyCheckbox->GetValue();
    params.Processing.pluginLang = ParallaxGenPlugin::getPluginLangFromString(
        m_processingPluginPatchingOptionsLangCombo->GetStringSelection().ToStdString());
    params.Processing.multithread = m_processingMultithreadingCheckbox->GetValue();
    params.Processing.bsa = m_processingBSACheckbox->GetValue();
    params.Processing.diagnostics = m_enableDiagnosticsCheckbox->GetValue();

    // Pre-Patchers
    params.PrePatcher.disableMLP = m_prePatcherDisableMLPCheckbox->GetValue();
    params.PrePatcher.fixMeshLighting = m_prePatcherFixMeshLightingCheckbox->GetValue();

    // Shader Patchers
    params.ShaderPatcher.parallax = m_shaderPatcherParallaxCheckbox->GetValue();
    params.ShaderPatcher.complexMaterial = m_shaderPatcherComplexMaterialCheckbox->GetValue();

    params.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist.clear();
    for (int i = 0; i < m_shaderPatcherComplexMaterialDynCubemapBlocklist->GetItemCount(); i++) {
        const auto itemText = m_shaderPatcherComplexMaterialDynCubemapBlocklist->GetItemText(i);
        if (!itemText.IsEmpty()) {
            params.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist.push_back(
                itemText.ToStdWstring());
        }
    }

    params.ShaderPatcher.truePBR = m_shaderPatcherTruePBRCheckbox->GetValue();
    params.ShaderPatcher.ShaderPatcherTruePBR.checkPaths = m_shaderPatcherTruePBRCheckPathsCheckbox->GetValue();
    params.ShaderPatcher.ShaderPatcherTruePBR.printNonExistentPaths
        = m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox->GetValue();

    // Shader Transforms
    params.ShaderTransforms.parallaxToCM = m_shaderTransformParallaxToCMCheckbox->GetValue();

    // Post-Patchers
    params.PostPatcher.fixSSS = m_postPatcherFixSSSCheckbox->GetValue();
    params.PostPatcher.hairFlowMap = m_postPatcherHairFlowMapCheckbox->GetValue();

    // Global Patchers
    params.GlobalPatcher.fixEffectLightingCS = m_globalPatcherFixEffectLightingCSCheckbox->GetValue();

    // Mesh Rules
    params.MeshRules.allowList.clear();
    for (int i = 0; i < m_meshRulesAllowList->GetItemCount(); i++) {
        const auto itemText = m_meshRulesAllowList->GetItemText(i);
        if (!itemText.IsEmpty()) {
            params.MeshRules.allowList.push_back(itemText.ToStdWstring());
        }
    }

    params.MeshRules.blockList.clear();
    for (int i = 0; i < m_meshRulesBlockList->GetItemCount(); i++) {
        const auto itemText = m_meshRulesBlockList->GetItemText(i);
        if (!itemText.IsEmpty()) {
            params.MeshRules.blockList.push_back(itemText.ToStdWstring());
        }
    }

    // Texture Rules
    params.TextureRules.textureMaps.clear();
    for (int i = 0; i < m_textureRulesMaps->GetItemCount(); i++) {
        const auto itemText = m_textureRulesMaps->GetItemText(i, 0);
        if (!itemText.IsEmpty()) {
            const auto texType = NIFUtil::getTexTypeFromStr(m_textureRulesMaps->GetItemText(i, 1).ToStdString());
            params.TextureRules.textureMaps.emplace_back(itemText.ToStdWstring(), texType);
        }
    }

    params.TextureRules.vanillaBSAList.clear();
    for (int i = 0; i < m_textureRulesVanillaBSAList->GetItemCount(); i++) {
        const auto itemText = m_textureRulesVanillaBSAList->GetItemText(i);
        if (!itemText.IsEmpty()) {
            params.TextureRules.vanillaBSAList.push_back(itemText.ToStdWstring());
        }
    }
}

void LauncherWindow::onBrowseGameLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(this, "Select Game Location", m_gameLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_gameLocationTextbox->SetValue(dialog.GetPath());
    }
}

void LauncherWindow::onBrowseMO2InstanceLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(this, "Select MO2 Instance Location", m_mo2InstanceLocationTextbox->GetValue());
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
    if (!m_modManagerRadios[ModManagerDirectory::ModManagerType::MODORGANIZER2]->GetValue()) {
        // If MO2 is not selected, clear the instance location textbox and return
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(true);
        }
        return;
    }

    const auto instanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Check if modorganizer.ini exists
    if (!ModManagerDirectory::isValidMO2InstanceDir(instanceDir)) {
        // set instance directory text to red
        m_mo2InstanceLocationTextbox->SetForegroundColour(*wxRED);
        return;
    }

    // set instance directory text to black
    m_mo2InstanceLocationTextbox->SetForegroundColour(*wxBLACK);

    // Get game path
    const auto gamePathMO2 = ModManagerDirectory::getGamePathFromInstanceDir(instanceDir);
    if (!gamePathMO2.empty()) {
        // found the game path, set it to the game location textbox
        m_gameLocationTextbox->SetValue(gamePathMO2.wstring());
        // disable the game location textbox
        m_gameLocationTextbox->Enable(false);
        m_gameLocationBrowseButton->Enable(false);
    } else {
        // not found, enable the game location textbox
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
    }

    // Get game type
    const auto gameTypeMO2 = ModManagerDirectory::getGameTypeFromInstanceDir(instanceDir);
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
    wxDirDialog dialog(this, "Select Output Location", m_outputLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_outputLocationTextbox->SetValue(dialog.GetPath());
    }
}

void LauncherWindow::onListEdit(wxListEvent& event)
{
    auto* listCtrl = dynamic_cast<wxListCtrl*>(event.GetEventObject());
    if (listCtrl == nullptr) {
        return;
    }

    if (event.IsEditCancelled()) {
        return;
    }

    const auto editedIndex = event.GetIndex();
    const auto& editedText = event.GetLabel();

    if (editedText.IsEmpty() && editedIndex != listCtrl->GetItemCount() - 1) {
        // If the edited item is empty and it's not the last item
        this->CallAfter([listCtrl, editedIndex]() { listCtrl->DeleteItem(editedIndex); });
        return;
    }

    // If the edited item is not empty and it's the last item
    if (!editedText.IsEmpty() && editedIndex == listCtrl->GetItemCount() - 1) {
        // Add a new empty item
        listCtrl->InsertItem(listCtrl->GetItemCount(), "");
    }
}

void LauncherWindow::onListItemActivated(wxListEvent& event)
{
    auto* listCtrl = dynamic_cast<wxListCtrl*>(event.GetEventObject());
    if (listCtrl == nullptr) {
        return;
    }

    const auto itemIndex = event.GetIndex();

    // Start editing the label of the item
    listCtrl->EditLabel(itemIndex);
}

void LauncherWindow::updateDisabledElements()
{
    ParallaxGenConfig::PGParams curParams = m_pgc.getParams();
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
}

void LauncherWindow::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // All validation passed, proceed with OK actions
        EndModal(wxID_OK);
    }
}

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
        = wxMessageBox("Are you sure you want to load the config from the file? This action will overwrite all "
                       "current unsaved settings.",
            "Confirm Load Config", wxYES_NO | wxICON_WARNING, this);

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
        = wxMessageBox("Are you sure you want to restore the default settings? This action cannot be undone.",
            "Confirm Restore Defaults", wxYES_NO | wxICON_WARNING, this);

    if (response != wxYES) {
        return;
    }

    // Reset the config to the default
    m_pgc.setParams(ParallaxGenConfig::getDefaultParams());

    loadConfig();

    updateDisabledElements();
}

auto LauncherWindow::saveConfig() -> bool
{
    vector<string> errors;
    ParallaxGenConfig::PGParams params = m_pgc.getParams();
    getParams(params);

    // Validate the parameters
    if (!ParallaxGenConfig::validateParams(params, errors)) {
        wxMessageDialog errorDialog(this, boost::algorithm::join(errors, "\n"), "Errors", wxOK | wxICON_ERROR);
        errorDialog.ShowModal();
        return false;
    }

    m_pgc.setParams(params);
    m_pgc.saveUserConfig();
    return true;
}

void LauncherWindow::onClose([[maybe_unused]] wxCloseEvent& event)
{
    ParallaxGenHandlers::nonBlockingExit();
    wxTheApp->Exit();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
