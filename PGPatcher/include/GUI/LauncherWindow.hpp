#pragma once

#include <wx/arrstr.h>
#include <wx/dnd.h>
#include <wx/dragimag.h>
#include <wx/event.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msw/textctrl.h>
#include <wx/overlay.h>
#include <wx/sizer.h>
#include <wx/wx.h>

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenConfig.hpp"

/**
 * @brief wxDialog that allows the user to configure the ParallaxGen parameters.
 */
class LauncherWindow : public wxDialog {
public:
    /**
     * @brief Construct a new Launcher Window object
     *
     * @param pgc PGC object for UI to use
     */
    LauncherWindow(ParallaxGenConfig& pgc, std::filesystem::path cacheDir);

    /**
     * @brief Get the Params object (meant to be called after the user presses okay)
     *
     * @return ParallaxGenConfig::PGParams Gets the current parameters set by the user on the dialog
     */
    [[nodiscard]] auto getParams() -> ParallaxGenConfig::PGParams;

private:
    constexpr static int DEFAULT_WIDTH = 600;
    constexpr static int DEFAULT_HEIGHT = 800;
    constexpr static int BORDER_SIZE = 5;
    constexpr static int BUTTON_FONT_SIZE = 12;

    std::filesystem::path m_cacheDir; /** Path to the cache directory */

    ParallaxGenConfig& m_pgc; /** Reference to the ParallaxGenConfig object */

    /**
     * @brief Runs immediately after the wxDialog gets constructed, intended to set the UI elements to the initial
     * parameters
     *
     * @param event wxWidgets event object
     */
    void onInitDialog(wxInitDialogEvent& event);

    /**
     * @brief Loads config from PGC
     */
    void loadConfig();

    //
    // UI Param Elements
    //

    // Game
    wxTextCtrl* m_gameLocationTextbox;
    void onGameLocationChange(wxCommandEvent& event);

    std::unordered_map<BethesdaGame::GameType, wxRadioButton*> m_gameTypeRadios;
    void onGameTypeChange(wxCommandEvent& event);

    // Mod Manager
    std::unordered_map<ModManagerDirectory::ModManagerType, wxRadioButton*> m_modManagerRadios;
    void onModManagerChange(wxCommandEvent& event);

    wxTextCtrl* m_mo2InstanceLocationTextbox;
    void onMO2InstanceLocationChange(wxCommandEvent& event);

    wxChoice* m_mo2ProfileChoice;
    void onMO2ProfileChange(wxCommandEvent& event);

    wxCheckBox* m_mo2UseOrderCheckbox;
    void onMO2UseOrderChange(wxCommandEvent& event);

    // Output
    wxTextCtrl* m_outputLocationTextbox;
    void onOutputLocationChange(wxCommandEvent& event);

    wxCheckBox* m_outputZipCheckbox;
    void onOutputZipChange(wxCommandEvent& event);

    // Advanced
    wxCheckBox* m_advancedOptionsCheckbox;
    void onAdvancedOptionsChange(wxCommandEvent& event);

    // Processing
    wxCheckBox* m_processingPluginPatchingCheckbox;
    void onProcessingPluginPatchingChange(wxCommandEvent& event);
    wxStaticBoxSizer* m_processingPluginPatchingOptions;
    wxCheckBox* m_processingPluginPatchingOptionsESMifyCheckbox;
    void onProcessingPluginPatchingOptionsESMifyChange(wxCommandEvent& event);

    wxCheckBox* m_processingMultithreadingCheckbox;
    void onProcessingMultithreadingChange(wxCommandEvent& event);

    wxCheckBox* m_processingBSACheckbox;
    void onProcessingBSAChange(wxCommandEvent& event);

    wxCheckBox* m_enableDiagnosticsCheckbox;
    void onEnableDiagnosticsChange(wxCommandEvent& event);

    // Pre-Patchers
    wxCheckBox* m_prePatcherDisableMLPCheckbox;
    void onPrePatcherDisableMLPChange(wxCommandEvent& event);

    wxCheckBox* m_prePatcherFixMeshLightingCheckbox;
    void onPrePatcherFixMeshLightingChange(wxCommandEvent& event);

    // Shader Patchers
    wxCheckBox* m_shaderPatcherParallaxCheckbox;
    void onShaderPatcherParallaxChange(wxCommandEvent& event);

    wxCheckBox* m_shaderPatcherComplexMaterialCheckbox;
    void onShaderPatcherComplexMaterialChange(wxCommandEvent& event);

    wxStaticBoxSizer* m_shaderPatcherComplexMaterialOptionsSizer; /** Stores the complex material options */
    wxListCtrl* m_shaderPatcherComplexMaterialDynCubemapBlocklist;
    void onShaderPatcherComplexMaterialDynCubemapBlocklistChange(wxListEvent& event);

    wxCheckBox* m_shaderPatcherTruePBRCheckbox;
    void onShaderPatcherTruePBRChange(wxCommandEvent& event);

    wxStaticBoxSizer* m_shaderPatcherTruePBROptionsSizer;
    wxCheckBox* m_shaderPatcherTruePBRCheckPathsCheckbox;
    void onShaderPatcherTruePBRCheckPathsChange(wxCommandEvent& event);

    wxCheckBox* m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox;
    void onShaderPatcherTruePBRPrintNonExistentPathsChange(wxCommandEvent& event);

    // Shader Transforms
    wxCheckBox* m_shaderTransformParallaxToCMCheckbox;
    void onShaderTransformParallaxToCMChange(wxCommandEvent& event);

    // Post-Patchers
    wxCheckBox* m_postPatcherOptimizeMeshesCheckbox;
    void onPostPatcherOptimizeMeshesChange(wxCommandEvent& event);

    wxCheckBox* m_postPatcherFixSSSCheckbox;
    void onPostPatcherFixSSSChange(wxCommandEvent& event);

    // Mesh Rules
    wxListCtrl* m_meshRulesAllowList;
    void onMeshRulesAllowListChange(wxListEvent& event);

    wxListCtrl* m_meshRulesBlockList;
    void onMeshRulesBlockListChange(wxListEvent& event);

    // Texture Rules
    wxListCtrl* m_textureRulesMaps;
    void onTextureRulesMapsChange(wxListEvent& event);
    void onTextureRulesMapsChangeStart(wxMouseEvent& event);

    wxListCtrl* m_textureRulesVanillaBSAList;
    void onTextureRulesVanillaBSAListChange(wxListEvent& event);

    //
    // UI Controls
    //
    wxStaticBoxSizer* m_mo2OptionsSizer; /** Stores the MO2-specific options since these are only sometimes shown */
    wxStaticBoxSizer* m_processingOptionsSizer; /** Stores the processing options */

    wxComboBox* m_textureMapTypeCombo; /** Stores the texture map type combo box */

    /**
     * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for
     * game location
     *
     * @param event wxWidgets event object
     */
    void onBrowseGameLocation(wxCommandEvent& event);

    /**
     * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for MO2
     * instance location
     *
     * @param event wxWidgets event object
     */
    void onBrowseMO2InstanceLocation(wxCommandEvent& event);

    /**
     * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for
     * output location
     *
     * @param event wxWidgets event object
     */
    void onBrowseOutputLocation(wxCommandEvent& event);

    /**
     * @brief Updates disabled elements based on the current state of the UI
     */
    void updateDisabledElements();

    wxBoxSizer* m_advancedOptionsSizer; /** Container that stores advanced options */
    void updateAdvanced();

    /**
     * @brief Event handler responsible for deleting/adding items based on list edits
     *
     * @param event wxWidgets event object
     */
    void onListEdit(wxListEvent& event);

    /**
     * @brief Event handler responsible for activating list items on double click or enter
     *
     * @param event wxWidgets event object
     */
    void onListItemActivated(wxListEvent& event);

    auto getColumnAtPosition(const wxPoint& pos, long item) -> int;

    //
    // Validation
    //
    wxButton* m_okButton; /** Stores the OKButton as a member var in case it needs to be disabled/enabled */
    wxButton*
        m_saveConfigButton; /** Stores the SaveConfigButton as a member var in case it needs to be disabled/enabled */
    wxButton* m_loadConfigButton;

    /**
     * @brief Event handler that triggers when the user presses "Start Patching" - performs validation
     *
     * @param event wxWidgets event object
     */
    void onOkButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the "Save Config" button
     *
     * @param event wxWidgets event object
     */
    void onSaveConfigButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the "Load Config" button
     *
     * @param event wxWidgets event object
     */
    void onLoadConfigButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the "Restore Defaults" button
     *
     * @param event wxWidgets event object
     */
    void onRestoreDefaultsButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the X on the dialog window, which closes the application
     *
     * @param event wxWidgets event object
     */
    void onClose(wxCloseEvent& event);

    /**
     * @brief Saves current values to the config
     */
    auto saveConfig() -> bool;
};
