// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QWidget>

#include <array>
#include <memory>

OBS_DECLARE_MODULE()

namespace {
constexpr auto dockId = "darts-atlas-tournament";
constexpr auto ownerKey = "darts-atlas-tournament-role";
constexpr auto defaultCss = "body { background-color: rgba(0, 0, 0, 0); margin: 0; overflow: hidden; }";

template <typename T, void (*Release)(T *)> using ObsRef = std::unique_ptr<T, decltype(Release)>;
using Source = ObsRef<obs_source_t, obs_source_release>;
using Scene = ObsRef<obs_scene_t, obs_scene_release>;
using Data = ObsRef<obs_data_t, obs_data_release>;

struct Display {
	const char *sceneName;
	const char *sourceName;
	const char *route;
	bool refresh;
};

constexpr std::array<Display, 2> displays{{
	{"Darts Atlas - Group Stage", "Darts Atlas - Live Groups Page", "live_groups", false},
	{"Darts Atlas - Knockout", "Darts Atlas - Bracket Page", "bracket", true},
}};

struct SceneSetup {
	Scene scene{nullptr, obs_scene_release};
	Source browser{nullptr, obs_source_release};
	Data settings{nullptr, obs_data_release};
	obs_sceneitem_t *addedItem = nullptr;
	bool createdScene = false;
	bool createdBrowser = false;
};

QString takePath(char *path)
{
	const QString result = path ? QString::fromUtf8(path) : QString{};
	bfree(path);
	return result;
}

QString tournamentBase(const QString &input)
{
	static const QRegularExpression pattern(
		R"(\A(?i:https://(?:www\.)?dartsatlas\.com)/tournaments/([A-Za-z0-9]+)/?(?:\?[^#\s]*)?(?:#[^\s]*)?\z)");
	const auto match = pattern.match(input.trimmed());
	if (!match.hasMatch())
		return {};
	return QStringLiteral("https://www.dartsatlas.com/tournaments/") + match.captured(1);
}

bool hasRole(obs_source_t *source, const QByteArray &role)
{
	const Data settings(obs_source_get_private_settings(source), obs_data_release);
	return role == obs_data_get_string(settings.get(), ownerKey);
}

void setRole(obs_source_t *source, const QByteArray &role)
{
	const Data settings(obs_source_get_private_settings(source), obs_data_release);
	obs_data_set_string(settings.get(), ownerKey, role.constData());
}

QString prepareScene(SceneSetup &setup, const Display &display)
{
	const QByteArray sceneRole = QByteArray("scene:") + display.route;
	const QByteArray browserRole = QByteArray("browser:") + display.route;
	const Source existing(obs_get_source_by_name(display.sceneName), obs_source_release);
	if (existing) {
		if (!hasRole(existing.get(), sceneRole) || !obs_scene_from_source(existing.get()))
			return QStringLiteral("Scene name already in use: %1").arg(QString::fromUtf8(display.sceneName));
		setup.scene.reset(obs_scene_get_ref(obs_scene_from_source(existing.get())));
	} else {
		setup.scene.reset(obs_scene_create(display.sceneName));
		if (!setup.scene)
			return QStringLiteral("Could not create the display scene.");
		setup.createdScene = true;
		setRole(obs_scene_get_source(setup.scene.get()), sceneRole);
	}

	setup.browser.reset(obs_get_source_by_name(display.sourceName));
	if (setup.browser) {
		if (!hasRole(setup.browser.get(), browserRole) ||
		    QByteArray(obs_source_get_id(setup.browser.get())) != "browser_source")
			return QStringLiteral("Source name already in use: %1").arg(QString::fromUtf8(display.sourceName));
	} else {
		setup.browser.reset(obs_source_create("browser_source", display.sourceName, setup.settings.get(), nullptr));
		if (!setup.browser)
			return QStringLiteral("Could not create a Browser Source. Check that OBS Browser Source is installed.");
		setup.createdBrowser = true;
		setRole(setup.browser.get(), browserRole);
	}

	if (!obs_scene_find_source(setup.scene.get(), display.sourceName)) {
		setup.addedItem = obs_scene_add(setup.scene.get(), setup.browser.get());
		if (!setup.addedItem)
			return QStringLiteral("Could not add the Browser Source to its scene.");
	}
	return {};
}

void undoSetup(SceneSetup &setup)
{
	if (setup.addedItem)
		obs_sceneitem_remove(setup.addedItem);
	if (setup.createdBrowser)
		obs_source_remove(setup.browser.get());
	if (setup.createdScene)
		obs_source_remove(obs_scene_get_source(setup.scene.get()));
}

class TournamentDock final : public QWidget {
public:
	TournamentDock()
	{
		auto *layout = new QFormLayout(this);
		url = new QLineEdit(this);
		url->setPlaceholderText(QStringLiteral("https://www.dartsatlas.com/tournaments/…"));
		theme = new QComboBox(this);
		auto *load = new QPushButton(QStringLiteral("Load"), this);
		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addRow(QStringLiteral("Competition URL"), url);
		layout->addRow(QStringLiteral("Theme"), theme);
		layout->addRow(load);
		layout->addRow(status);
		connect(load, &QPushButton::clicked, this, [this] { loadTournament(); });
		connect(url, &QLineEdit::returnPressed, load, &QPushButton::click);
		const auto themePath = takePath(obs_module_config_path("themes"));
		if (!themePath.isEmpty() && QDir().mkpath(themePath)) {
			installedThemes = themePath;
			theme->setToolTip(QStringLiteral("Install themes in %1. Reopen the dock to refresh the list.").arg(themePath));
		}
		updateThemes();
	}

protected:
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		updateThemes();
	}

private:
	void updateThemes()
	{
		const QString selected = theme->currentText();
		const QSignalBlocker blocker(theme);
		theme->clear();
		theme->addItem(QStringLiteral("DA Default"));
		const auto builtIn = takePath(obs_module_file("themes/BHDP Teal"));
		if (!builtIn.isEmpty())
			theme->addItem(QStringLiteral("BHDP Teal"), builtIn);
		if (!installedThemes.isEmpty()) {
			const QDir folder(installedThemes);
			for (const auto &name : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
				const auto path = folder.absoluteFilePath(name);
				if (theme->findText(name) < 0 && QFileInfo(path + "/live_groups.css").isFile() &&
				    QFileInfo(path + "/bracket.css").isFile())
					theme->addItem(name, path);
			}
		}
		const int index = theme->findText(selected);
		theme->setCurrentIndex(index >= 0 ? index : 0);
	}

	void loadTournament()
	{
		const QString base = tournamentBase(url->text());
		if (base.isEmpty()) {
			status->setText(QStringLiteral("Enter a Darts Atlas tournament URL."));
			return;
		}

		obs_video_info video{};
		if (!obs_get_video_info(&video)) {
			status->setText(QStringLiteral("OBS video is unavailable. Check Settings → Video."));
			return;
		}

		std::array<SceneSetup, displays.size()> setups;
		for (size_t i = 0; i < displays.size(); ++i) {
			const auto &display = displays[i];
			const QString directory = theme->currentData().toString();
			QByteArray css(defaultCss);
			if (!directory.isEmpty()) {
				QFile file(QDir(directory).filePath(QString::fromLatin1(display.route) + ".css"));
				if (!file.open(QIODevice::ReadOnly)) {
					status->setText(QStringLiteral("Could not open %1 in the selected theme.").arg(file.fileName()));
					return;
				}
				css = file.readAll();
				if (file.error() != QFileDevice::NoError || css.trimmed().isEmpty() || css.contains('\0')) {
					status->setText(QStringLiteral("Could not read valid CSS from %1.").arg(file.fileName()));
					return;
				}
			}

			auto &settings = setups[i].settings;
			settings.reset(obs_data_create());
			const QByteArray pageUrl = (base + '/' + QString::fromLatin1(display.route)).toUtf8();
			obs_data_set_string(settings.get(), "url", pageUrl.constData());
			obs_data_set_string(settings.get(), "css", css.constData());
			obs_data_set_bool(settings.get(), "is_local_file", false);
			obs_data_set_bool(settings.get(), "restart_when_active", display.refresh);
			obs_data_set_bool(settings.get(), "shutdown", display.refresh);
			obs_data_set_int(settings.get(), "width", video.base_width);
			obs_data_set_int(settings.get(), "height", video.base_height);
		}

		for (size_t i = 0; i < displays.size(); ++i) {
			const QString error = prepareScene(setups[i], displays[i]);
			if (!error.isEmpty()) {
				for (auto it = setups.rbegin(); it != setups.rend(); ++it)
					undoSetup(*it);
				status->setText(error);
				return;
			}
		}

		for (auto &setup : setups)
			obs_source_update(setup.browser.get(), setup.settings.get());
		if (obs_frontend_preview_program_mode_active())
			obs_frontend_set_current_preview_scene(obs_scene_get_source(setups.front().scene.get()));
		obs_frontend_save();

		status->setText(QStringLiteral("Scenes configured for %1 with %2.").arg(base.section('/', -1), theme->currentText()));
	}

	QLineEdit *url = nullptr;
	QComboBox *theme = nullptr;
	QLabel *status = nullptr;
	QString installedThemes;
};
} // namespace

bool obs_module_load(void)
{
	auto dock = std::make_unique<TournamentDock>();
	if (!obs_frontend_add_dock_by_id(dockId, "Darts Atlas", dock.get()))
		return false;
	dock.release();
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_dock(dockId);
}
