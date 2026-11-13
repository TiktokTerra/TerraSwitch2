// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <QWidget>
#include "common/common_types.h"
#include "common/settings.h"

class QBoxLayout;
class QHBoxLayout;
class QLabel;
class QMainWindow;
class QMenu;
class QPushButton;
class QStandardItemModel;
class QTimer;
class QToolButton;

namespace FritchiiUi {

/// The Performance button's one-click graphics presets, from fastest to prettiest.
enum class PerfPreset { SuperFps, MaxFps, Performance, Balanced, Quality, MaxQuality };

/// The graphics settings a preset writes. Super FPS keeps them to put back when it's turned off.
struct PerformanceState {
    Settings::VSyncMode vsync;
    Settings::ResolutionSetup resolution;
    Settings::ScalingFilter filter;
    Settings::AntiAliasing aa;
    Settings::GpuAccuracy accuracy;
    bool async_shaders;
    bool frame_gen;
    u32 frame_gen_target;
};

/// What the home screen asks the main window to do.
struct HomeHooks {
    std::function<void(const QString& path, u64 program_id)> play;
    std::function<void()> add_folder;
    std::function<void()> open_settings;
    std::function<void()> save_config; ///< after the Performance presets changed settings
    std::function<void()> apply_settings; ///< and pushed them into a running game
    std::function<void(u64 program_id, const std::string& path)> game_settings;
    std::function<void(u64 program_id)> delete_shader_cache; ///< after a confirmation
    /// Fills a menu with the game list's own right-click entries for the game (mod folders,
    /// dumps, Remove, Verify Integrity, Configure Game...).
    std::function<void(QMenu& menu, u64 program_id, const std::string& path)> game_menu;
    std::function<bool()> game_list_empty;
    std::function<bool()> single_window;
    std::function<bool()> emulation_running;
    QStandardItemModel* model = nullptr;
    QMenu* help_menu = nullptr;
    QList<QMenu*> classic_menus;  ///< File, Game, View, Tools, Help behind the rail's menu button
};

/// The Fritchii main screen: a left icon rail (Library, Settings, Help, and the classic menus
/// behind a button) and a home page with one big card for Ocarina of Time (or the first game)
/// and the other games in a small row. The classic game list stays one click away
/// ("All games"); the main window keeps managing it as before.
class Home : public QObject {
    Q_OBJECT
public:
    Home(QMainWindow* window, QBoxLayout* central, QWidget* game_list, QWidget* placeholder,
         HomeHooks hooks);

    /// A game is starting in the main window: the render view takes the space.
    void OnEmulationStarting();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ShowHome();
    void ShowList();
    void Restore();
    void Refresh();
    void RefreshChips();

    /// The Performance menu: graphics presets, frame generation up to x8 and the FPS target.
    void OpenPerformanceMenu(QWidget* anchor);

    /// Writes one graphics preset into the settings, then saves and applies them. Picking
    /// Super FPS while it is on turns it off again and puts the previous settings back.
    void ApplyPreset(PerfPreset preset);
    /// Writes a whole settings state (the one Super FPS replaced).
    void ApplyState(const PerformanceState& state);
    /// Lights the pill of the preset the settings currently match, and names it in the chip.
    void UpdatePresetButtons();

    QWidget* MakeRail();
    QWidget* MakePage();
    QToolButton* RailButton(const QString& icon, const QString& tip);
    QLabel* MakeChip(QWidget* parent);

    QMainWindow* window;
    QWidget* game_list;
    QWidget* placeholder;
    HomeHooks hooks;

    QWidget* rail = nullptr;
    QWidget* page = nullptr;
    QToolButton* library_button = nullptr;
    QToolButton* performance_button = nullptr;
    QLabel* cover = nullptr;
    QLabel* title = nullptr;
    QLabel* eyebrow = nullptr;
    QLabel* meta = nullptr;
    QLabel* chip_preset = nullptr;
    QLabel* chip_renderer = nullptr;
    QLabel* chip_frame_gen = nullptr;
    QLabel* chip_vsync = nullptr;
    QLabel* section_count = nullptr;
    QPushButton* play = nullptr;
    QHBoxLayout* row = nullptr;
    QWidget* row_box = nullptr;
    QTimer* refresh_timer = nullptr;
    std::vector<QPushButton*> preset_buttons;

    /// The settings Super FPS replaced, while it runs.
    std::optional<PerformanceState> before_super_fps;

    bool list_mode = false;
    QString hero_path;
    u64 hero_id = 0;
};

} // namespace FritchiiUi
