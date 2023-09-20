#include "gln.h"

CEditor *editor;
static CPopup *curPopup;

static bool Draw_Directory(CFileEntry *entry);

static inline bool ItemWithTooltip(const char *item_name, const char *fmt, ...)
{
    const bool pressed = ImGui::MenuItem(item_name);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        va_list argptr;

        va_start(argptr, fmt);
        ImGui::SetTooltipV(fmt, argptr);
        va_end(argptr);
    }
    return pressed;
}

static inline bool ButtonWithTooltip(const char *item_name, const char *fmt, ...)
{
    const bool pressed = ImGui::Button(item_name);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        va_list argptr;

        va_start(argptr, fmt);
        ImGui::SetTooltipV(fmt, argptr);
        va_end(argptr);
    }
    return pressed;
}

static inline bool MenuWithTooltip(const char *item_name, const char *fmt, ...)
{
    const bool pressed = ImGui::BeginMenu(item_name);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        va_list argptr;

        va_start(argptr, fmt);
        ImGui::SetTooltipV(fmt, argptr);
        va_end(argptr);
    }
    return pressed;
}

CEditor::CEditor(void)
    : mConsoleActive{ false }
{
    mConfig = new CGameConfig;
}

/*
RecursiveDirectoryIterator: iterates through the given and all the subdirectories
*/
static void RecursiveDirectoryIterator(const std::filesystem::path& path, std::vector<CFileEntry>& fileList)
{
    if (!std::filesystem::directory_entry{ path }.is_directory()) {
        return;
    }
    for (const auto& it : std::filesystem::directory_iterator{ path }) {
        fileList.emplace_back(it.path(), it.is_directory());
        if (it.is_directory()) {
            RecursiveDirectoryIterator(it.path(), fileList);
        }
    }
}

/*
DirectoryIterator: iterates through the given path, does not recurse into subdirectories
*/
static void DirectoryIterator(const std::filesystem::path& path, std::vector<CFileEntry>& fileList)
{
    if (!std::filesystem::directory_entry{ path }.is_directory()) {
        return;
    }
    for (const auto& it : std::filesystem::directory_iterator{ path }) {
        fileList.emplace_back(it.path(), it.is_directory());
    }
}

void CEditor::ReloadFileCache(void)
{
    Printf("[CEditor::ReloadFileCache] reloading file cache...");
    RecursiveDirectoryIterator(mConfig->mEditorPath.c_str(), mFileCache);
}

static void Draw_Widgets(void)
{
    std::vector<CWidget>& widgets = editor->mWidgets;

    for (auto& it : widgets) {
        if (it.mActive) {
            it.mActive = ImGui::Begin(it.mName.c_str(), &it.mActive, it.mFlags);
            it.mDrawFunc(std::addressof(it));
            ImGui::End();
        }
    }
}

static void Draw_Popups(void)
{
    std::list<CPopup>& popups = editor->mPopups;

    if (!popups.size()) {
        return;
    }

    if (!curPopup) {
        curPopup = &popups.back();
    }

    ImGui::OpenPopup(curPopup->mName.c_str());

    if (ImGui::BeginPopupModal(curPopup->mName.c_str())) {
        ImGui::Text("%s", curPopup->mMsg.c_str());
        if (ImGui::Button("DONE")) {
            ImGui::CloseCurrentPopup();
            curPopup = NULL;
            popups.pop_back();
        }
        ImGui::EndPopup();
    }
}

CFileEntry *Draw_FileList(std::vector<CFileEntry>& fileList)
{
    for (auto& it : fileList) {
        if (it.mIsDir && ImGui::BeginMenu(it.mPath.c_str())) {
            Draw_FileList(it.mDirList);
            ImGui::EndMenu();
        }
        else if (ImGui::MenuItem(it.mPath.c_str())) {
            return std::addressof(it);
        }
    }
    return NULL;
}

static void File_Menu(void)
{
    if (ImGui::BeginMenu("Project")) {
        if (ItemWithTooltip("Open Project", "Open an already made project")) {

        }
        if (ItemWithTooltip("New Project", "Create a new project")) {

        }
        if (ItemWithTooltip("Save Project", "Save your current project")) {
            
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Open Recent")) {
        ImGui::EndMenu();
    }
}

static void Edit_Preferences(void);
static void Edit_Project(void);
static void Edit_Map(void);
static void Edit_Checkpoint(void);
static void Edit_Spawn(void);

static void Edit_Menu(void)
{
    if (ImGui::BeginMenu("Preferences")) {
        Edit_Preferences();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Map")) {
        Edit_Map();
        ImGui::EndMenu();
    }
}

static void Build_Menu(void)
{
    if (ItemWithTooltip("Build Map", "Save the current map in text-based format into a .map file")) {
        Map_Save(mapData.mName.c_str());
    }
    if (ItemWithTooltip("Compile Map", "Compile a .map file into a .bmf file,\nNOTE: .bmf files cannot be used in the map editor")) {

    }
}

void CEditor::AddPopup(const CPopup& popup)
{ editor->mPopups.emplace_back(popup); }
CWidget *CEditor::PushWidget(const CWidget& widget)
{ return std::addressof(editor->mWidgets.emplace_back(widget)); }

static CFileEntry enginePath{std::string(pwdString.string() + "Data/glnomad" EXE_EXT).c_str(), false};
static CFileEntry exePath{std::string(pwdString.string() + "Data/").c_str(), true};
static bool exePathChanged = false;
static bool enginePathChanged = false;

void CEditor::Draw(void)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            File_Menu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            Edit_Menu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Build")) {
            Build_Menu();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    Draw_Popups();
    Draw_Widgets();

    Edit_Checkpoint();
    Edit_Spawn();

    if (ImGuiFileDialog::Instance()->IsOpened("SelectEnginePathDlg")) {
        if (ImGuiFileDialog::Instance()->Display("SelectEnginePathDlg", ImGuiWindowFlags_NoResize, ImVec2( 1012, 641 ), ImVec2( 1012, 641 ))) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                enginePath.mPath = ImGuiFileDialog::Instance()->GetFilePathName();
                enginePathChanged = true;
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }
    if (ImGuiFileDialog::Instance()->IsOpened("SelectExePathDlg")) {
        if (ImGuiFileDialog::Instance()->Display("SelectExePathDlg", ImGuiWindowFlags_NoResize, ImVec2( 1012, 641 ), ImVec2( 1012, 641 ))) {
            ImGui::SetWindowSize(ImVec2( 641, 1012 ));
            if (ImGuiFileDialog::Instance()->IsOk()) {
                exePath.mPath = ImGuiFileDialog::Instance()->GetFilePathName();
                exePathChanged = true;
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }
}

static bool techy = false, changed = false;
static bool textureDetailsChanged = false;
static bool textureFiltersChanged = false;
static int textureDetails, textureFilters;

static void Edit_Preferences(void)
{
    ImGui::SeparatorText("Configuration");
    if (ButtonWithTooltip(va("Engine Path: %s", editor->mConfig->mEnginePath.c_str()), "Set the editor's path to the game engine")) {
        ImGuiFileDialog::Instance()->OpenDialog("SelectEnginePathDlg", "Select Executable File", ".*, .exe, .application, .sh, .AppImage", editor->mConfig->mEditorPath);
    }
    if (ButtonWithTooltip(va("Executable Path: %s", editor->mConfig->mExecutablePath.c_str()), "Set the editor's path to executables")) {
        ImGuiFileDialog::Instance()->OpenDialog("SelectExePathDlg", "Select Folder", "/", editor->mConfig->mEditorPath);
    }

    ImGui::SeparatorText("Graphics");
    if (ImGui::BeginMenu("Texture Detail")) {
        for (uint64_t i = 0; i < arraylen(texture_details); i++) {
            if (ImGui::MenuItem(texture_details[i].s)) {
                textureDetailsChanged = true;
                changed = true;
                textureDetails = texture_details[i].i;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Texture Filtering")) {
        if (!techy && ImGui::Button("I'm A Techie")) {
            techy = true;
        }
        else if (techy && ImGui::Button("I'm Not A Techie")) {
            techy = false;
        }
        if (techy) {
            ImGui::Text("Mag Filter | Min Filter | Filter Name");
            ImGui::Text("-----------|------------|------------");
        }
        for (uint64_t i = 0; i < arraylen(texture_filters); i++) {
            if (ImGui::MenuItem(techy ? texture_filters_alt[i].s : texture_filters[i].s)) {
                textureFiltersChanged = true;
                changed = true;
                textureFilters = texture_filters[i].i;
            }
        }
        ImGui::EndMenu();
    }
    
    if (changed) {
        if (ImGui::Button("Save Preferences")) {
            if (enginePathChanged) {
                editor->mConfig->mEnginePath = enginePath.mPath;
                enginePathChanged = false;
            }
            if (exePathChanged) {
                editor->mConfig->mExecutablePath = exePath.mPath;
                exePathChanged = false;
            }
            if (textureDetailsChanged) {
                editor->mConfig->mTextureDetail = texture_details[textureDetails].i;
                textureDetailsChanged = false;
            }
            if (textureFiltersChanged) {
                editor->mConfig->mTextureFiltering = texture_filters[textureFilters].i;
                textureFiltersChanged = false;
            }
            changed = false;
            editor->mConfig->mPrefs.SavePrefs();
        }
    }
}

namespace MapPrefs {

static bool changedName = false;
static bool changedWidth = false;
static bool changedHeight = false;
static bool changedCheckpoints = false;
static bool changedSpawns = false;
static bool changed = false;
static int width = 16;
static int height = 16;
static int numCheckpoints = 0;
static int numSpawns = 1;
static uint64_t editingCheckpointsIndex = 0;
static uint64_t editingSpawnsIndex = 0;
static bool editingCheckpoints = false;
static bool editingSpawns = false;
static char name[1024];

};

namespace MapSpawn {

static int x = 0;
static int y = 0;
static int z = 0;
static bool changedX = false;
static bool changedY = false;
static bool changedZ = false;

};

static void Edit_Spawn(void)
{
    using namespace MapSpawn;
    mapspawn_t *s;
    bool open;
    const uint64_t index = MapPrefs::editingSpawnsIndex;

    if (!MapPrefs::editingSpawns)
        return;
    
    s = &mapData.mSpawns[index];
    if (!changedX) {
        x = s->xyz[0];
    }
    if (!changedY) {
        y = s->xyz[1];
    }

    open = true;
    if (ImGui::Begin(va("Editing Spawn %lu##EditSpawn", index), &open, ImGuiWindowFlags_NoResize)) {
        ImGui::SetWindowSize({ 223, 103 });
        if (ImGui::InputInt("x", &x)) {
            changedX = true;
        }
        if (ImGui::InputInt("y", &y)) {
            changedY = true;
        }
        if (ImGui::Button("Save Spawn")) {
            if (changedX) {
                changedX = false;
                s->xyz[0] = x;
                x = 0;
            }
            if (changedY) {
                changedY = false;
                s->xyz[1] = y;
                y = 0;
            }
            if (changedZ) {
                changedZ = false;
                s->xyz[2] = z;
                z = 0;
            }
            MapPrefs::editingCheckpoints = false;
        }
    }
    ImGui::End();
    MapPrefs::editingCheckpoints = open;
}

namespace MapCheckpoint {

static int x = 0;
static int y = 0;
static int z = 0;
static bool changedX = false;
static bool changedY = false;
static bool changedZ = false;

};

static void Edit_Checkpoint(void)
{
    mapcheckpoint_t *c;
    bool open;
    const uint64_t index = MapPrefs::editingCheckpointsIndex;

    if (!MapPrefs::editingCheckpoints)
        return;
    
    c = &mapData.mCheckpoints[index];
    if (!MapCheckpoint::changedX) {
        MapCheckpoint::x = c->xyz[0];
    }
    if (!MapCheckpoint::changedY) {
        MapCheckpoint::y = c->xyz[1];
    }

    open = true;
    if (ImGui::Begin(va("Editing Checkpoint %lu##EditCheckpoint", index), &open, ImGuiWindowFlags_NoResize)) {
        ImGui::SetWindowSize({ 223, 103 });
        if (ImGui::InputInt("x", &MapCheckpoint::x)) {
            MapCheckpoint::changedX = true;
        }
        if (ImGui::InputInt("y", &MapCheckpoint::y)) {
            MapCheckpoint::changedY = true;
        }
        if (ImGui::Button("Save Checkpoint")) {
            if (MapCheckpoint::changedX) {
                MapCheckpoint::changedX = false;
                c->xyz[0] = MapCheckpoint::x;
                MapCheckpoint::x = 0;
            }
            if (MapCheckpoint::changedY) {
                MapCheckpoint::changedY = false;
                c->xyz[1] = MapCheckpoint::y;
                MapCheckpoint::y = 0;
            }
            if (MapCheckpoint::changedZ) {
                MapCheckpoint::changedZ = false;
                c->xyz[2] = MapCheckpoint::z;
                MapCheckpoint::z = 0;
            }
            MapPrefs::editingCheckpoints = false;
        }
    }
    ImGui::End();
    MapPrefs::editingCheckpoints = open;
}

static void Edit_Map(void)
{
    ImGui::SeparatorText("Parameters");
    if (!MapPrefs::changedName && !MapPrefs::changed) {
        N_strncpyz(MapPrefs::name, mapData.mName.c_str(), sizeof(MapPrefs::name));
    }
    if (!MapPrefs::changedCheckpoints && !MapPrefs::changed) {
        MapPrefs::numCheckpoints = mapData.mCheckpoints.size();
    }
    if (!MapPrefs::changedSpawns && !MapPrefs::changed) {
        MapPrefs::numSpawns = mapData.mSpawns.size();
    }

    if (ImGui::InputText("Name", MapPrefs::name, sizeof(MapPrefs::name))) {
        MapPrefs::changedName = true;
        MapPrefs::changed = true;
    }
    if (ImGui::InputInt("Width", &MapPrefs::width)) {
        MapPrefs::changedWidth = true;
        MapPrefs::changed = true;
        if (MapPrefs::width < 16) {
            MapPrefs::width = 16;
        }
        else if (MapPrefs::width > 1024) {
            MapPrefs::width = 1024;
        }
    }
    if (ImGui::InputInt("Height", &MapPrefs::height)) {
        MapPrefs::changedHeight = true;
        MapPrefs::changed = true;
        if (MapPrefs::height < 16) {
            MapPrefs::height = 16;
        }
        else if (MapPrefs::height > 1024) {
            MapPrefs::height = 1024;
        }
    }
    if (ImGui::InputInt("Checkpoint Count", &MapPrefs::numCheckpoints)) {
        MapPrefs::changedCheckpoints = true;
        MapPrefs::changed = true;
        if (MapPrefs::numCheckpoints < 0) {
            MapPrefs::numCheckpoints = 0;
        }
        else if (MapPrefs::numCheckpoints > 128) {
            MapPrefs::numCheckpoints = 128;
        }
    }
    if (ImGui::InputInt("Spawn Count", &MapPrefs::numSpawns)) {
        MapPrefs::changedSpawns = true;
        MapPrefs::changed = true;
        if (MapPrefs::numSpawns < 1) {
            MapPrefs::numSpawns = 1;
        }
        else if (MapPrefs::numSpawns > 1024) {
            MapPrefs::numSpawns = 1024;
        }
    }
    if (ImGui::BeginMenu("Edit Checkpoint")) {
        for (uint64_t i = 0; i < mapData.mCheckpoints.size(); i++) {
            if (ImGui::MenuItem(va("Checkpoint #%lu", i))) {
                MapPrefs::editingCheckpointsIndex = i;
                MapPrefs::editingCheckpoints = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit Spawn")) {
        for (uint64_t i = 0; i < mapData.mSpawns.size(); i++) {
            if (ImGui::MenuItem(va("Spawn #%lu", i))) {
                MapPrefs::editingSpawnsIndex = i;
                MapPrefs::editingSpawns = true;
            }
        }
        ImGui::EndMenu();
    }

    if (MapPrefs::changed) {
        if (ImGui::Button("Save Map")) {
            if (MapPrefs::changedName) {
                mapData.mName = MapPrefs::name;
                memset(MapPrefs::name, 0, sizeof(MapPrefs::name));
                MapPrefs::changedName = false;
            }
            if (MapPrefs::changedWidth) {
                mapData.mWidth = MapPrefs::width;
                MapPrefs::width = 0;
                mapData.mTiles.resize(mapData.mHeight * mapData.mWidth);
                MapPrefs::changedWidth = false;
            }
            if (MapPrefs::changedHeight) {
                mapData.mHeight = MapPrefs::height;
                MapPrefs::height = 0;
                mapData.mTiles.resize(mapData.mHeight * mapData.mWidth);
                MapPrefs::changedHeight = false;
            }
            if (MapPrefs::changedCheckpoints) {
                mapData.mCheckpoints.resize(MapPrefs::numCheckpoints);
                MapPrefs::changedCheckpoints = false;
                MapPrefs::numCheckpoints = 0;
            }
            if (MapPrefs::changedSpawns) {
                mapData.mSpawns.resize(MapPrefs::numSpawns);
                MapPrefs::changedSpawns = false;
                MapPrefs::numSpawns = 0;
            }
            MapPrefs::changed = false;
        }
    }
}
