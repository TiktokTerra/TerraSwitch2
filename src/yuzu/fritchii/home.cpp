// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/fritchii/home.h"

#include <algorithm>
#include <array>
#include <set>
#include <vector>

#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include "common/settings.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/fritchii/fritchii_ui.h"
#include "yuzu/fritchii/saves_window.h"

namespace FritchiiUi {
namespace {

constexpr u64 OcarinaOfTime = 0x0400486024DEC000ULL;
constexpr int CoverSize = 200;
constexpr int TileIcon = 96;
constexpr int MaxTiles = 7;

/// The graphics settings a Performance preset writes, and how the menu and the pills describe it.
struct PerfSpec {
    PerfPreset preset;
    const char* label;
    const char* detail;
    Settings::VSyncMode vsync;
    Settings::ResolutionSetup resolution;
    Settings::ScalingFilter filter;
    Settings::AntiAliasing aa;
    Settings::GpuAccuracy accuracy;
    bool async_shaders;
};

// Super FPS: half resolution, the cheapest filter, no anti-aliasing, no vsync; frame generation
// is turned off with it, since its FIFO present is what would hold the frames back. High FPS: the
// same "give me frames" setup at three-quarter resolution. Performance: three-quarter resolution
// with mailbox pacing. Balanced: native resolution. Quality: 1.5x FSR with FXAA. Maximum Quality:
// 2x FSR 3 with SMAA and the accurate GPU mode.
constexpr std::array<PerfSpec, 6> kPerfPresets{{
    {PerfPreset::SuperFps, "Super FPS",
     "0.5x resolution, no anti-aliasing, no vsync, frame generation off: the most frames.",
     Settings::VSyncMode::Immediate, Settings::ResolutionSetup::Res1_2X,
     Settings::ScalingFilter::Bilinear, Settings::AntiAliasing::None, Settings::GpuAccuracy::Low,
     true},
    {PerfPreset::MaxFps, "High FPS", "0.75x resolution, no anti-aliasing, no vsync.",
     Settings::VSyncMode::Immediate, Settings::ResolutionSetup::Res3_4X,
     Settings::ScalingFilter::Bilinear, Settings::AntiAliasing::None, Settings::GpuAccuracy::Low,
     true},
    {PerfPreset::Performance, "Performance",
     "0.75x resolution, no anti-aliasing, vsync (mailbox).", Settings::VSyncMode::Mailbox,
     Settings::ResolutionSetup::Res3_4X, Settings::ScalingFilter::ScaleForce,
     Settings::AntiAliasing::None, Settings::GpuAccuracy::Low, true},
    {PerfPreset::Balanced, "Balanced", "Native resolution, no anti-aliasing.",
     Settings::VSyncMode::Mailbox, Settings::ResolutionSetup::Res1X,
     Settings::ScalingFilter::ScaleForce, Settings::AntiAliasing::None, Settings::GpuAccuracy::Low,
     true},
    {PerfPreset::Quality, "Quality", "1.5x FSR upscaling with FXAA.", Settings::VSyncMode::Mailbox,
     Settings::ResolutionSetup::Res3_2X, Settings::ScalingFilter::Fsr,
     Settings::AntiAliasing::Fxaa, Settings::GpuAccuracy::Low, true},
    {PerfPreset::MaxQuality, "Maximum Quality",
     "2x FSR 3 upscaling with SMAA and the accurate GPU mode.", Settings::VSyncMode::Mailbox,
     Settings::ResolutionSetup::Res2X, Settings::ScalingFilter::Fsr3,
     Settings::AntiAliasing::Smaa, Settings::GpuAccuracy::High, true},
}};

/// The FPS targets the Performance menu offers for frame generation (0 keeps the multiplier).
constexpr std::array<u32, 7> kFrameGenTargets{{0, 60, 90, 120, 144, 165, 240}};

/// "1x", "0.75x", "1.5x"... for the hero's resolution chip.
QString ResolutionLabel(Settings::ResolutionSetup setup) {
    switch (setup) {
    case Settings::ResolutionSetup::Res1_4X:
        return QStringLiteral("0.25x");
    case Settings::ResolutionSetup::Res1_2X:
        return QStringLiteral("0.5x");
    case Settings::ResolutionSetup::Res3_4X:
        return QStringLiteral("0.75x");
    case Settings::ResolutionSetup::Res1X:
        return QStringLiteral("1x");
    case Settings::ResolutionSetup::Res5_4X:
        return QStringLiteral("1.25x");
    case Settings::ResolutionSetup::Res3_2X:
        return QStringLiteral("1.5x");
    case Settings::ResolutionSetup::Res2X:
        return QStringLiteral("2x");
    case Settings::ResolutionSetup::Res3X:
        return QStringLiteral("3x");
    case Settings::ResolutionSetup::Res4X:
        return QStringLiteral("4x");
    case Settings::ResolutionSetup::Res5X:
        return QStringLiteral("5x");
    case Settings::ResolutionSetup::Res6X:
        return QStringLiteral("6x");
    case Settings::ResolutionSetup::Res7X:
        return QStringLiteral("7x");
    case Settings::ResolutionSetup::Res8X:
        return QStringLiteral("8x");
    default:
        return QStringLiteral("1x");
    }
}

/// The preset the current settings match, if any (for the menu's check mark).
[[nodiscard]] const PerfSpec* MatchingPerfPreset() {
    for (const PerfSpec& spec : kPerfPresets) {
        if (Settings::values.vsync_mode.GetValue() == spec.vsync &&
            Settings::values.resolution_setup.GetValue() == spec.resolution &&
            Settings::values.scaling_filter.GetValue() == spec.filter &&
            Settings::values.anti_aliasing.GetValue() == spec.aa &&
            Settings::values.gpu_accuracy.GetValue() == spec.accuracy &&
            Settings::values.use_asynchronous_shaders.GetValue() == spec.async_shaders) {
            return &spec;
        }
    }
    return nullptr;
}

/// The settings the presets write, read from the current ones (Super FPS puts them back).
[[nodiscard]] PerformanceState CurrentPerformanceState() {
    return {Settings::values.vsync_mode.GetValue(),
            Settings::values.resolution_setup.GetValue(),
            Settings::values.scaling_filter.GetValue(),
            Settings::values.anti_aliasing.GetValue(),
            Settings::values.gpu_accuracy.GetValue(),
            Settings::values.use_asynchronous_shaders.GetValue(),
            Settings::values.frame_gen.GetValue(),
            Settings::values.frame_gen_target_rate.GetValue()};
}

struct Game {
    QString title;
    QString path;
    u64 program_id = 0;
    QPixmap icon;
};

QPixmap Rounded(const QPixmap& source, int size, int radius) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap out(QSize(size, size) * ratio);
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
        painter.setClipPath(clip);
        if (source.isNull()) {
            painter.fillRect(QRect(0, 0, size, size), QColor(0x2b, 0x21, 0x44));
        } else {
            painter.drawPixmap(QRect(0, 0, size, size), source);
        }
    }
    return out;
}

/// True for a missing or fully transparent icon (some dumps have none).
bool IsBlank(const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return true;
    }
    const QImage image = pixmap.toImage();
    const int step_x = std::max(1, image.width() / 16);
    const int step_y = std::max(1, image.height() / 16);
    for (int y = 0; y < image.height(); y += step_y) {
        for (int x = 0; x < image.width(); x += step_x) {
            if (qAlpha(image.pixel(x, y)) != 0) {
                return false;
            }
        }
    }
    return true;
}

/// A cover the player put next to the game file: "<file name>.png" or "icon.png" (the same files
/// Game Settings > Detect Icon looks at first). Read only; the game file is never changed.
QPixmap LocalCover(const QString& game_path) {
    const QFileInfo info(game_path);
    const QDir dir = info.absoluteDir();
    for (const QString& base : {info.completeBaseName(), QStringLiteral("icon")}) {
        for (const char* ext : {".png", ".jpg", ".jpeg", ".webp"}) {
            const QFileInfo candidate(dir.filePath(base + QLatin1String(ext)));
            if (!candidate.isFile() || candidate.size() > 16 * 1024 * 1024) {
                continue;
            }
            QPixmap pixmap;
            if (pixmap.load(candidate.filePath())) {
                return pixmap;
            }
        }
    }
    return {};
}

std::vector<Game> CollectGames(QStandardItemModel* model, const QPixmap& fallback) {
    std::vector<Game> games;
    std::set<QString> seen;
    if (!model) {
        return games;
    }
    const auto add = [&](const QStandardItem* item) {
        if (item->data(GameListItem::TypeRole).toInt() !=
            static_cast<int>(GameListItemType::Game)) {
            return;
        }
        const QString path = item->data(GameListItemPath::FullPathRole).toString();
        if (path.isEmpty() || !seen.insert(path).second) {
            return;
        }
        Game game;
        game.path = path;
        game.program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
        game.title = item->data(GameListItemPath::TitleRole).toString();
        if (game.title.isEmpty()) {
            game.title = QFileInfo(path).completeBaseName();
        }
        const QByteArray icon_data = item->data(GameListItemPath::IconDataRole).toByteArray();
        if (!game.icon.loadFromData(icon_data)) {
            game.icon = item->data(Qt::DecorationRole).value<QPixmap>();
        }
        if (IsBlank(game.icon)) {
            game.icon = LocalCover(path);
        }
        if (IsBlank(game.icon)) {
            // No icon in the dump and none next to it: Fritchii's own icon, not an empty square.
            game.icon = fallback;
        }
        games.push_back(std::move(game));
    };
    for (int row = 0; row < model->rowCount(); ++row) {
        const QStandardItem* top = model->item(row);
        if (!top) {
            continue;
        }
        if (top->hasChildren()) {
            for (int child = 0; child < top->rowCount(); ++child) {
                add(top->child(child));
            }
        } else {
            add(top);
        }
    }
    return games;
}

} // namespace

Home::Home(QMainWindow* window_, QBoxLayout* central, QWidget* game_list_, QWidget* placeholder_,
           HomeHooks hooks_)
    : QObject(window_), window{window_}, game_list{game_list_}, placeholder{placeholder_},
      hooks{std::move(hooks_)} {
    // The classic menus live behind the rail's menu button. A zero-height bar keeps every
    // shortcut working and stays out of the way when fullscreen shows the bar again.
    window->menuBar()->setFixedHeight(0);

    rail = MakeRail();
    page = MakePage();
    central->insertWidget(0, rail);
    central->addWidget(page, 1);

    game_list->installEventFilter(this);
    placeholder->installEventFilter(this);

    refresh_timer = new QTimer(this);
    refresh_timer->setSingleShot(true);
    refresh_timer->setInterval(150);
    connect(refresh_timer, &QTimer::timeout, this, &Home::Refresh);
    if (hooks.model) {
        const auto later = [this] { refresh_timer->start(); };
        connect(hooks.model, &QAbstractItemModel::rowsInserted, this, later);
        connect(hooks.model, &QAbstractItemModel::rowsRemoved, this, later);
        connect(hooks.model, &QAbstractItemModel::modelReset, this, later);
        connect(hooks.model, &QAbstractItemModel::dataChanged, this, later);
    }
    QTimer::singleShot(0, this, &Home::Restore);
}

QToolButton* Home::RailButton(const QString& icon, const QString& tip) {
    auto* button = new QToolButton(rail);
    button->setObjectName(QStringLiteral("FritchiiRailButton"));
    button->setIcon(Icon(icon, 22));
    button->setIconSize(QSize(22, 22));
    button->setText(tip);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setToolTip(tip);
    button->setFixedSize(72, 60);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QWidget* Home::MakeRail() {
    rail = new QWidget;
    rail->setObjectName(QStringLiteral("FritchiiRail"));
    rail->setAttribute(Qt::WA_StyledBackground);
    rail->setFixedWidth(88);
    auto* layout = new QVBoxLayout(rail);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(6);

    // The TerraSwitch 2 logo on top of the rail.
    auto* logo = new QLabel(rail);
    logo->setPixmap(Rounded(QIcon(QStringLiteral(":/img/lichii.ico")).pixmap(256), 44, 11));
    logo->setFixedSize(44, 44);
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(14);

    library_button = RailButton(QStringLiteral("library"), tr("Library"));
    library_button->setCheckable(true);
    connect(library_button, &QToolButton::clicked, this, &Home::ShowHome);

    // One click to the presets, frame generation and the FPS target, even in list mode.
    performance_button = RailButton(QStringLiteral("gauge"), tr("Performance"));
    connect(performance_button, &QToolButton::clicked, this,
            [this] { OpenPerformanceMenu(performance_button); });

    QToolButton* settings = RailButton(QStringLiteral("settings"), tr("Settings"));
    connect(settings, &QToolButton::clicked, this, [this] { hooks.open_settings(); });

    QToolButton* help = RailButton(QStringLiteral("help"), tr("Help"));
    connect(help, &QToolButton::clicked, this, [this, help] {
        if (hooks.help_menu) {
            hooks.help_menu->exec(help->mapToGlobal(QPoint(help->width() + 8, 0)));
        }
    });

    auto* classic = new QMenu(rail);
    for (QMenu* menu : hooks.classic_menus) {
        classic->addMenu(menu);
    }
    QToolButton* menu_button = RailButton(QStringLiteral("menu"), tr("Menu"));
    connect(menu_button, &QToolButton::clicked, this, [classic, menu_button] {
        const QSize size = classic->sizeHint();
        classic->exec(menu_button->mapToGlobal(
            QPoint(menu_button->width() + 8, menu_button->height() - size.height())));
    });

    // Discord and Donate are on the home page itself (MakePage), with their names.
    layout->addWidget(library_button);
    layout->addWidget(performance_button);
    layout->addWidget(settings);
    layout->addWidget(help);
    layout->addStretch(1);
    layout->addWidget(menu_button);
    return rail;
}

QLabel* Home::MakeChip(QWidget* parent) {
    auto* chip = new QLabel(parent);
    chip->setObjectName(QStringLiteral("FritchiiChip"));
    chip->setAttribute(Qt::WA_StyledBackground);
    return chip;
}

QWidget* Home::MakePage() {
    page = new QWidget;
    page->setObjectName(QStringLiteral("FritchiiHome"));
    page->setAttribute(Qt::WA_StyledBackground);
    page->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(page, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (hero_path.isEmpty()) {
            return;
        }
        // The game list's full menu (modders use its folders and dumps), then Delete Shader Cache.
        QMenu menu;
        if (hooks.game_menu) {
            hooks.game_menu(menu, hero_id, hero_path.toStdString());
        } else {
            menu.addAction(tr("Game Settings..."), &menu, [this] {
                hooks.game_settings(hero_id, hero_path.toStdString());
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Delete Shader Cache..."), &menu,
                       [this] { hooks.delete_shader_cache(hero_id); });
        menu.exec(page->mapToGlobal(pos));
    });

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(52, 28, 52, 32);
    layout->setSpacing(0);

    // Top bar: the page's name, then Saves/Discord/Donate with their names on the right.
    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    auto* heading = new QLabel(tr("Library"), page);
    heading->setObjectName(QStringLiteral("FritchiiPageTitle"));
    top->addWidget(heading, 0, Qt::AlignVCenter);
    top->addStretch(1);

    const auto link = [this](const QString& icon, const QString& text, void (*open)()) {
        auto* button = new QPushButton(Icon(icon, 18), text, page);
        button->setObjectName(QStringLiteral("FritchiiLink"));
        button->setIconSize(QSize(18, 18));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [open] { open(); });
        return button;
    };
    // v5.8.3: the Saves window (export, import, copy between Fritchii, Eden and Lichii).
    auto* saves = new QPushButton(Icon(QStringLiteral("list"), 18), tr("Saves"), page);
    saves->setObjectName(QStringLiteral("FritchiiLink"));
    saves->setIconSize(QSize(18, 18));
    saves->setCursor(Qt::PointingHandCursor);
    saves->setFocusPolicy(Qt::NoFocus);
    connect(saves, &QPushButton::clicked, this, [this] {
        ShowSavesWindow(window, hooks.emulation_running);
    });
    top->addWidget(saves);
    top->addWidget(link(QStringLiteral("community"), tr("Discord"), &OpenDiscord));
    top->addWidget(link(QStringLiteral("heart"), tr("Donate"), &OpenDonate));
    layout->addLayout(top);
    layout->addSpacing(24);

    // Hero: the cover on a raised gradient card, then the title, live status chips and actions.
    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("FritchiiHero"));
    auto* card_layout = new QHBoxLayout(card);
    card_layout->setContentsMargins(28, 28, 32, 28);
    card_layout->setSpacing(34);

    cover = new QLabel(card);
    cover->setFixedSize(CoverSize, CoverSize);
    card_layout->addWidget(cover, 0, Qt::AlignVCenter);

    auto* side = new QVBoxLayout;
    side->setSpacing(0);
    side->addStretch(1);

    eyebrow = new QLabel(card);
    eyebrow->setObjectName(QStringLiteral("FritchiiEyebrow"));
    side->addWidget(eyebrow);
    side->addSpacing(8);

    title = new QLabel(card);
    title->setObjectName(QStringLiteral("FritchiiHeroTitle"));
    title->setWordWrap(true);
    title->setMaximumWidth(680);
    side->addWidget(title);
    side->addSpacing(8);

    meta = new QLabel(card);
    meta->setObjectName(QStringLiteral("FritchiiMeta"));
    side->addWidget(meta);
    side->addSpacing(16);

    auto* chips = new QHBoxLayout;
    chips->setSpacing(8);
    chip_preset = MakeChip(card);
    chip_renderer = MakeChip(card);
    chip_frame_gen = MakeChip(card);
    chip_vsync = MakeChip(card);
    chips->addWidget(chip_preset);
    chips->addWidget(chip_renderer);
    chips->addWidget(chip_frame_gen);
    chips->addWidget(chip_vsync);
    chips->addStretch(1);
    side->addLayout(chips);
    side->addSpacing(18);

    // The presets as pills: one click, and the current one stays lit. Super FPS is the mode that
    // turns everything down for the most frames; the tooltip says what each one changes.
    auto* preset_row = new QHBoxLayout;
    preset_row->setSpacing(6);
    for (const PerfSpec& spec : kPerfPresets) {
        auto* pill = new QPushButton(tr(spec.label), card);
        pill->setObjectName(QStringLiteral("FritchiiPreset"));
        pill->setCheckable(true);
        pill->setCursor(Qt::PointingHandCursor);
        pill->setFocusPolicy(Qt::NoFocus);
        pill->setToolTip(tr(spec.detail));
        connect(pill, &QPushButton::clicked, this,
                [this, preset = spec.preset] { ApplyPreset(preset); });
        preset_buttons.push_back(pill);
        preset_row->addWidget(pill);
    }
    preset_row->addStretch(1);
    side->addLayout(preset_row);
    side->addSpacing(20);

    play = new QPushButton(card);
    play->setObjectName(QStringLiteral("FritchiiPlay"));
    play->setCursor(Qt::PointingHandCursor);
    play->setIconSize(QSize(18, 18));
    connect(play, &QPushButton::clicked, this, [this] {
        if (hero_path.isEmpty()) {
            hooks.add_folder();
        } else {
            hooks.play(hero_path, hero_id);
        }
    });

    // Frame generation up to x8 and the FPS target; the presets are the pills above, and the rest
    // of the detail stays in the full settings dialog behind "All Settings...".
    auto* perf = new QPushButton(Icon(QStringLiteral("gauge"), 18), tr("Performance"), card);
    perf->setObjectName(QStringLiteral("FritchiiLink"));
    perf->setIconSize(QSize(18, 18));
    perf->setCursor(Qt::PointingHandCursor);
    perf->setFocusPolicy(Qt::NoFocus);
    connect(perf, &QPushButton::clicked, this, [this, perf] { OpenPerformanceMenu(perf); });

    auto* hero_actions = new QHBoxLayout;
    hero_actions->setSpacing(12);
    hero_actions->addWidget(play, 0, Qt::AlignVCenter);
    hero_actions->addWidget(perf, 0, Qt::AlignVCenter);
    hero_actions->addStretch(1);
    side->addLayout(hero_actions);
    side->addStretch(1);
    card_layout->addLayout(side, 1);
    layout->addWidget(card);

    layout->addSpacing(32);

    // The rest of the library: bigger tiles, the count and "All games" in the header, and an
    // "Add folder" tile at the end.
    auto* section = new QHBoxLayout;
    section->setSpacing(10);
    auto* section_title = new QLabel(tr("Your games"), page);
    section_title->setObjectName(QStringLiteral("FritchiiSectionTitle"));
    section->addWidget(section_title, 0, Qt::AlignVCenter);
    section_count = new QLabel(page);
    section_count->setObjectName(QStringLiteral("FritchiiSection"));
    section->addWidget(section_count, 0, Qt::AlignVCenter);
    section->addStretch(1);
    auto* all_games = new QPushButton(Icon(QStringLiteral("list"), 16), tr("All games"), page);
    all_games->setObjectName(QStringLiteral("FritchiiLinkSmall"));
    all_games->setIconSize(QSize(16, 16));
    all_games->setCursor(Qt::PointingHandCursor);
    all_games->setFocusPolicy(Qt::NoFocus);
    connect(all_games, &QPushButton::clicked, this, &Home::ShowList);
    section->addWidget(all_games, 0, Qt::AlignVCenter);
    layout->addLayout(section);
    layout->addSpacing(12);

    row_box = new QWidget(page);
    row = new QHBoxLayout(row_box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->setAlignment(Qt::AlignLeft);
    layout->addWidget(row_box);
    layout->addStretch(1);
    return page;
}

void Home::ApplyPreset(PerfPreset preset) {
    const PerfSpec* spec = nullptr;
    for (const PerfSpec& candidate : kPerfPresets) {
        if (candidate.preset == preset) {
            spec = &candidate;
            break;
        }
    }
    if (spec == nullptr) {
        return;
    }

    // Super FPS is a mode: picking it again leaves it and puts the previous settings back.
    if (preset == PerfPreset::SuperFps && MatchingPerfPreset() == spec) {
        if (before_super_fps) {
            ApplyState(*before_super_fps);
        } else {
            ApplyPreset(PerfPreset::Balanced);
        }
        return;
    }

    if (preset == PerfPreset::SuperFps) {
        if (!before_super_fps) {
            before_super_fps = CurrentPerformanceState();
        }
        // Nothing may cap the frames: frame generation forces FIFO, which holds them to the
        // display's refresh rate, and it turns off Fritchii's own frame pacing.
        Settings::values.frame_gen.SetValue(false);
        Settings::values.frame_gen_target_rate.SetValue(0);
    } else {
        before_super_fps.reset();
    }

    Settings::values.vsync_mode.SetValue(spec->vsync);
    Settings::values.resolution_setup.SetValue(spec->resolution);
    Settings::values.scaling_filter.SetValue(spec->filter);
    Settings::values.anti_aliasing.SetValue(spec->aa);
    Settings::values.gpu_accuracy.SetValue(spec->accuracy);
    Settings::values.use_asynchronous_shaders.SetValue(spec->async_shaders);

    if (hooks.save_config) {
        hooks.save_config();
    }
    if (hooks.apply_settings) {
        hooks.apply_settings();
    }
    RefreshChips();
}

void Home::ApplyState(const PerformanceState& state) {
    Settings::values.vsync_mode.SetValue(state.vsync);
    Settings::values.resolution_setup.SetValue(state.resolution);
    Settings::values.scaling_filter.SetValue(state.filter);
    Settings::values.anti_aliasing.SetValue(state.aa);
    Settings::values.gpu_accuracy.SetValue(state.accuracy);
    Settings::values.use_asynchronous_shaders.SetValue(state.async_shaders);
    Settings::values.frame_gen_target_rate.SetValue(state.frame_gen_target);
    Settings::values.frame_gen.SetValue(state.frame_gen);
    before_super_fps.reset();
    if (hooks.save_config) {
        hooks.save_config();
    }
    if (hooks.apply_settings) {
        hooks.apply_settings();
    }
    RefreshChips();
}

void Home::UpdatePresetButtons() {
    const PerfSpec* current = MatchingPerfPreset();
    for (size_t i = 0; i < preset_buttons.size() && i < kPerfPresets.size(); ++i) {
        preset_buttons[i]->setChecked(&kPerfPresets[i] == current);
    }
    if (chip_preset == nullptr) {
        return;
    }
    chip_preset->setText(current ? tr(current->label) : tr("Custom"));
    // The Super FPS mode is the one chip that stands out (violet), like the Play button.
    const bool super = current != nullptr && current->preset == PerfPreset::SuperFps;
    if (chip_preset->property("super").toBool() != super) {
        chip_preset->setProperty("super", super);
        chip_preset->style()->unpolish(chip_preset);
        chip_preset->style()->polish(chip_preset);
    }
}

void Home::OpenPerformanceMenu(QWidget* anchor) {
    QMenu menu(anchor);
    const PerfSpec* current = MatchingPerfPreset();
    auto* presets = new QActionGroup(&menu);
    for (const PerfSpec& spec : kPerfPresets) {
        QAction* action = menu.addAction(tr(spec.label));
        action->setCheckable(true);
        action->setChecked(&spec == current);
        action->setToolTip(tr(spec.detail));
        presets->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, preset = spec.preset] { ApplyPreset(preset); });
    }
    menu.addSeparator();

    // Frame generation: off, or 2x to 8x. Lossless Scaling stops at x4, so picking more switches
    // to FSR 3, whose optical flow has no such limit.
    QMenu* frame_gen = menu.addMenu(tr("Frame Generation"));
    const bool active = Settings::values.frame_gen.GetValue();
    QAction* off = frame_gen->addAction(tr("Off"));
    off->setCheckable(true);
    off->setChecked(!active);
    connect(off, &QAction::triggered, this, [this] {
        Settings::values.frame_gen.SetValue(false);
        if (hooks.save_config) {
            hooks.save_config();
        }
        RefreshChips();
    });
    frame_gen->addSeparator();
    const u32 multiplier = Settings::FrameGenMultiplier();
    for (u32 value = Settings::MIN_FRAME_GEN_MULTIPLIER;
         value <= Settings::MAX_FRAME_GEN_MULTIPLIER; ++value) {
        QAction* action = frame_gen->addAction(tr("%1x").arg(value));
        action->setCheckable(true);
        action->setChecked(active && multiplier == value);
        connect(action, &QAction::triggered, this, [this, value] {
            Settings::values.frame_gen_multiplier.SetValue(value);
            if (value > 4) {
                Settings::values.frame_gen_type.SetValue(Settings::FrameGenType::Fsr3);
            }
            Settings::values.frame_gen.SetValue(true);
            if (hooks.save_config) {
                hooks.save_config();
            }
            RefreshChips();
        });
    }
    frame_gen->addSeparator();
    auto* kinds = new QActionGroup(frame_gen);
    QAction* lsfg = frame_gen->addAction(tr("Lossless Scaling"));
    lsfg->setCheckable(true);
    lsfg->setChecked(Settings::values.frame_gen_type.GetValue() == Settings::FrameGenType::Lsfg);
    kinds->addAction(lsfg);
    connect(lsfg, &QAction::triggered, this, [this] {
        Settings::values.frame_gen_type.SetValue(Settings::FrameGenType::Lsfg);
        if (Settings::values.frame_gen_multiplier.GetValue() > 4) {
            Settings::values.frame_gen_multiplier.SetValue(4);
        }
        if (hooks.save_config) {
            hooks.save_config();
        }
        RefreshChips();
    });
    QAction* fsr3 = frame_gen->addAction(tr("Optical Flow (FSR 3)"));
    fsr3->setCheckable(true);
    fsr3->setChecked(Settings::values.frame_gen_type.GetValue() == Settings::FrameGenType::Fsr3);
    kinds->addAction(fsr3);
    connect(fsr3, &QAction::triggered, this, [this] {
        Settings::values.frame_gen_type.SetValue(Settings::FrameGenType::Fsr3);
        if (hooks.save_config) {
            hooks.save_config();
        }
        RefreshChips();
    });

    // Max FPS: the rate frame generation aims for; "Fixed multiplier" keeps the setting above.
    QMenu* fps = menu.addMenu(tr("Max FPS"));
    const u32 target = Settings::values.frame_gen_target_rate.GetValue();
    for (const u32 rate : kFrameGenTargets) {
        QAction* action =
            fps->addAction(rate == 0 ? tr("Fixed multiplier") : tr("%1 FPS").arg(rate));
        action->setCheckable(true);
        action->setChecked(target == rate);
        connect(action, &QAction::triggered, this, [this, rate] {
            Settings::values.frame_gen_target_rate.SetValue(rate);
            if (rate != 0 && !Settings::values.frame_gen.GetValue()) {
                Settings::values.frame_gen.SetValue(true);
            }
            if (hooks.save_config) {
                hooks.save_config();
            }
            RefreshChips();
        });
    }

    menu.addSeparator();
    menu.addAction(tr("All Settings..."), this, [this] { hooks.open_settings(); });

    const QPoint at = anchor->parentWidget() == rail
                          ? QPoint(anchor->width() + 8, 0)
                          : QPoint(0, anchor->height() + 6);
    menu.exec(anchor->mapToGlobal(at));
}

void Home::RefreshChips() {
    if (!chip_renderer) {
        return;
    }
    QString api = tr("OpenGL");
    switch (Settings::values.renderer_backend.GetValue()) {
    case Settings::RendererBackend::Vulkan:
        api = QStringLiteral("Vulkan");
        break;
    case Settings::RendererBackend::Null:
        api = QStringLiteral("Null");
        break;
    default:
        break;
    }
    chip_renderer->setText(
        tr("%1 at %2").arg(api, ResolutionLabel(Settings::values.resolution_setup.GetValue())));

    chip_frame_gen->setText(Settings::values.frame_gen.GetValue()
                                ? tr("Frame generation %1x").arg(Settings::FrameGenMultiplier())
                                : tr("Frame generation off"));

    QString sync = tr("mailbox");
    switch (Settings::values.vsync_mode.GetValue()) {
    case Settings::VSyncMode::Immediate:
        sync = tr("unlocked");
        break;
    case Settings::VSyncMode::Mailbox:
        break;
    case Settings::VSyncMode::Fifo:
        sync = tr("vsync");
        break;
    case Settings::VSyncMode::FifoRelaxed:
        sync = tr("vsync relaxed");
        break;
    }
    chip_vsync->setText(tr("Sync %1").arg(sync));
    UpdatePresetButtons();
}

void Home::Refresh() {
    const std::vector<Game> games = CollectGames(hooks.model, window->windowIcon().pixmap(256));

    // The hero: the game picked last, else Ocarina of Time, else the first game.
    const Game* hero = nullptr;
    for (const Game& game : games) {
        if (!hero_path.isEmpty() && game.path == hero_path) {
            hero = &game;
            break;
        }
    }
    if (!hero) {
        for (const Game& game : games) {
            if (game.program_id == OcarinaOfTime) {
                hero = &game;
                break;
            }
        }
    }
    if (!hero && !games.empty()) {
        hero = &games.front();
    }

    if (hero) {
        hero_path = hero->path;
        hero_id = hero->program_id;
        cover->setPixmap(Rounded(hero->icon, CoverSize, 18));
        cover->show();
        eyebrow->setText(tr("SELECTED GAME"));
        title->setText(hero->title);
        meta->setText(tr("Ready to play · %1 games").arg(games.size()));
        play->setText(tr("Play"));
        play->setIcon(Glyph(QStringLiteral("play"), 0x1c1433, 18));
    } else {
        hero_path.clear();
        hero_id = 0;
        cover->hide();
        eyebrow->setText(tr("GET STARTED"));
        title->setText(tr("Add your games"));
        meta->setText(tr("Point TerraSwitch 2 at the folder that holds your games"));
        play->setText(tr("Add Folder"));
        play->setIcon(Glyph(QStringLiteral("plus"), 0x1c1433, 18));
    }
    RefreshChips();
    if (section_count) {
        section_count->setText(games.empty() ? tr("No games yet")
                                             : tr("%1 games").arg(games.size()));
    }

    // The small row: the other games, then "Add folder" ("All games" is in the section header).
    // deleteLater: a tile's own click can land here.
    while (QLayoutItem* item = row->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto tile = [this](const QIcon& icon, const QString& text) {
        auto* button = new QToolButton(row_box);
        button->setObjectName(QStringLiteral("FritchiiTile"));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(icon);
        button->setIconSize(QSize(TileIcon, TileIcon));
        button->setFixedWidth(TileIcon + 40);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        const QFontMetrics metrics(button->font());
        button->setText(metrics.elidedText(text, Qt::ElideRight, TileIcon + 24));
        button->setToolTip(text);
        row->addWidget(button);
        return button;
    };
    int shown = 0;
    for (const Game& game : games) {
        if (game.path == hero_path || shown == MaxTiles) {
            continue;
        }
        ++shown;
        QToolButton* button = tile(QIcon(Rounded(game.icon, TileIcon, 12)), game.title);
        connect(button, &QToolButton::clicked, this,
                [this, path = game.path, id = game.program_id] {
                    hero_path = path;
                    hero_id = id;
                    Refresh();
                });
    }
    QToolButton* add = tile(Icon(QStringLiteral("plus"), 40), tr("Add folder"));
    add->setIconSize(QSize(40, 40));
    add->setMinimumHeight(TileIcon + 44);
    connect(add, &QToolButton::clicked, this, [this] { hooks.add_folder(); });
}

void Home::ShowHome() {
    list_mode = false;
    game_list->hide();
    placeholder->hide();
    rail->show();
    page->show();
    library_button->setChecked(true);
    Refresh();
}

void Home::ShowList() {
    list_mode = true;
    page->hide();
    rail->show();
    library_button->setChecked(false);
    if (hooks.game_list_empty()) {
        placeholder->show();
    } else {
        game_list->show();
    }
}

void Home::Restore() {
    // A restart shows the list for a moment, and nothing may cover a running game.
    if (hooks.single_window() && hooks.emulation_running()) {
        return;
    }
    if (list_mode) {
        rail->show();
        page->hide();
    } else {
        ShowHome();
    }
}

void Home::OnEmulationStarting() {
    if (!hooks.single_window()) {
        return;
    }
    rail->hide();
    page->hide();
}

bool Home::eventFilter(QObject* watched, QEvent* event) {
    // The main window shows its game list (start up, a game stopped, the list filled): show the
    // home page instead, unless the player asked for the full list.
    if (event->type() == QEvent::Show && (watched == game_list || watched == placeholder)) {
        QTimer::singleShot(0, this, &Home::Restore);
    }
    return QObject::eventFilter(watched, event);
}

} // namespace FritchiiUi
