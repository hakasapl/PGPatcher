#pragma once

#include <wx/arrstr.h>
#include <wx/dnd.h>
#include <wx/dragimag.h>
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
  static inline bool UIExitTriggered = false;

  /**
   * @brief Construct a new Launcher Window object
   *
   * @param Params initial set of parameters to have the dialog display
   */
  LauncherWindow(ParallaxGenConfig &PGC);

  /**
   * @brief Get the Params object (meant to be called after the user presses okay)
   *
   * @return ParallaxGenConfig::PGParams Gets the current parameters set by the user on the dialog
   */
  [[nodiscard]] auto getParams() -> ParallaxGenConfig::PGParams;

private:
  ParallaxGenConfig &PGC; /** Reference to the ParallaxGenConfig object */

  /**
   * @brief Runs immediately after the wxDialog gets constructed, intended to set the UI elements to the initial
   * parameters
   *
   * @param Event wxWidgets event object
   */
  void onInitDialog(wxInitDialogEvent &Event);

  //
  // UI Param Elements
  //

  // Game
  wxTextCtrl *GameLocationTextbox;
  void onGameLocationChange(wxCommandEvent &Event);

  std::unordered_map<BethesdaGame::GameType, wxRadioButton *> GameTypeRadios;
  void onGameTypeChange(wxCommandEvent &Event);

  // Mod Manager
  std::unordered_map<ModManagerDirectory::ModManagerType, wxRadioButton *> ModManagerRadios;
  void onModManagerChange(wxCommandEvent &Event);

  wxTextCtrl *MO2InstanceLocationTextbox;
  void onMO2InstanceLocationChange(wxCommandEvent &Event);

  wxChoice *MO2ProfileChoice;
  void onMO2ProfileChange(wxCommandEvent &Event);

  wxCheckBox *MO2UseOrderCheckbox;
  void onMO2UseOrderChange(wxCommandEvent &Event);

  // Output
  wxTextCtrl *OutputLocationTextbox;
  void onOutputLocationChange(wxCommandEvent &Event);

  wxCheckBox *OutputZipCheckbox;
  void onOutputZipChange(wxCommandEvent &Event);

  // Processing
  wxCheckBox *ProcessingPluginPatchingCheckbox;
  void onProcessingPluginPatchingChange(wxCommandEvent &Event);

  wxCheckBox *ProcessingMultithreadingCheckbox;
  void onProcessingMultithreadingChange(wxCommandEvent &Event);

  wxCheckBox *ProcessingHighMemCheckbox;
  void onProcessingHighMemChange(wxCommandEvent &Event);

  wxCheckBox *ProcessingGPUAccelerationCheckbox;
  void onProcessingGPUAccelerationChange(wxCommandEvent &Event);

  wxCheckBox *ProcessingMapFromMeshesCheckbox;
  void onProcessingMapFromMeshesChange(wxCommandEvent &Event);

  wxCheckBox *ProcessingBSACheckbox;
  void onProcessingBSAChange(wxCommandEvent &Event);


  // Pre-Patchers
  wxCheckBox *PrePatcherDisableMLPCheckbox;
  void onPrePatcherDisableMLPChange(wxCommandEvent &Event);

  // Shader Patchers
  wxCheckBox *ShaderPatcherParallaxCheckbox;
  void onShaderPatcherParallaxChange(wxCommandEvent &Event);

  wxCheckBox *ShaderPatcherComplexMaterialCheckbox;
  void onShaderPatcherComplexMaterialChange(wxCommandEvent &Event);

  wxCheckBox *ShaderPatcherTruePBRCheckbox;
  void onShaderPatcherTruePBRChange(wxCommandEvent &Event);

  // Shader Transforms
  wxCheckBox *ShaderTransformParallaxToCMCheckbox;
  void onShaderTransformParallaxToCMChange(wxCommandEvent &Event);

  // Post-Patchers
  wxCheckBox *PostPatcherOptimizeMeshesCheckbox;
  void onPostPatcherOptimizeMeshesChange(wxCommandEvent &Event);

  //
  // UI Controls
  //
  wxStaticBoxSizer *MO2OptionsSizer; /** Stores the MO2-specific options since these are only sometimes shown */

  /**
   * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for game
   * location
   *
   * @param Event wxWidgets event object
   */
  void onBrowseGameLocation(wxCommandEvent &Event);

  /**
   * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for MO2
   * instance location
   *
   * @param Event wxWidgets event object
   */
  void onBrowseMO2InstanceLocation(wxCommandEvent &Event);

  /**
   * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for
   * output location
   *
   * @param Event wxWidgets event object
   */
  void onBrowseOutputLocation(wxCommandEvent &Event);

  /**
   * @brief Updates disabled elements based on the current state of the UI
   */
  void updateDisabledElements();

  wxBoxSizer *AdvancedOptionsSizer; /** Container that stores advanced options */
  wxButton *AdvancedButton;         /** Button to show/hide advanced options */
  bool AdvancedVisible = false;     /** Stores whether advanced options are shown or not */

  /**
   * @brief Event handler responsible for showing/hiding the advanced options when the button is pressed
   *
   * @param Event wxWidgets event object
   */
  void onToggleAdvanced(wxCommandEvent &Event);

  //
  // Validation
  //
  wxButton *OKButton; /** Stores the OKButton as a member var in case it needs to be disabled/enabled */
  wxButton *SaveConfigButton; /** Stores the SaveConfigButton as a member var in case it needs to be disabled/enabled */

  /**
   * @brief Event handler that triggers when the user presses "Start Patching" - performs validation
   *
   * @param Event wxWidgets event object
   */
  void onOkButtonPressed(wxCommandEvent &Event);

  /**
   * @brief Event handler that triggers when the user presses the "Save Config" button
   *
   * @param Event wxWidgets event object
   */
  void onSaveConfigButtonPressed(wxCommandEvent &Event);

  /**
   * @brief Event handler that triggers when the user presses the X on the dialog window, which closes the application
   *
   * @param Event wxWidgets event object
   */
  void onClose(wxCloseEvent &Event);

  /**
   * @brief Saves current values to the config
   */
  void saveConfig();
};
