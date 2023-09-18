#include "gln.h"
#include "preferences.h"

#define GLNOMAD_ENGINE "GLNE"
#define GLNOMAD_EXE "glnomad" STR(EXE_EXT)

CPrefs::CPrefs(void)
{
}

CPrefs::~CPrefs()
{
    SavePrefs();
}

static void PrintPrefs_f(void)
{
    const std::vector<CPrefData>& prefList = editor->mConfig->mPrefs.mPrefList;
    for (std::vector<CPrefData>::const_iterator it = prefList.cbegin(); it != prefList.cend(); ++it) {
        Printf(
            "[%s] =>\n"
            "\tValue: %s\n"
            "\tGroup: %s\n"
        , it->mName.c_str(), it->mValue.c_str(), it->mGroup.c_str());
    }
}

void CPrefs::LoadPrefs(const std::string& path)
{
    json data;
    if (!LoadJSON(data, path)) {
        Printf("[CPrefs::LoadPrefs] Warning: failed to load preferences file, setting to default values");
        SetDefault();
        return;
    }

    mFilePath = path;

    mPrefList.reserve(data["config"].size());
    mPrefList.emplace_back("enginePath", data["config"]["enginePath"].get<std::string>().c_str(), "config");
    mPrefList.emplace_back("exePath", data["config"]["exePath"].get<std::string>().c_str(), "config");

    mPrefList.reserve(data["graphics"].size());
    mPrefList.emplace_back("textureDetail", data["graphics"]["textureDetail"].get<std::string>().c_str(), "config");
    mPrefList.emplace_back("textureFiltering", data["graphics"]["textureFiltering"].get<std::string>().c_str(), "config");

    mPrefList.reserve(data["camera"].size());
    mPrefList.emplace_back("moveSpeed", data["camera"]["moveSpeed"].get<std::string>().c_str(), "camera");
    mPrefList.emplace_back("rotationSpeed", data["camera"]["rotationSpeed"].get<std::string>().c_str(), "camera");
    mPrefList.emplace_back("zoomSpeed", data["camera"]["zoomSpeed"].get<std::string>().c_str(), "camera");
}

void CPrefs::SetDefault(void)
{
    mPrefList.reserve(8);

    mPrefList.emplace_back("enginePath", "", "config");
    mPrefList.emplace_back("exePath", "", "config");
    mPrefList.emplace_back("textureDetail", "2", "config");
    mPrefList.emplace_back("textureFiltering", "Bilinear", "config");
    mPrefList.emplace_back("moveSpeed", "1.5f", "camera");
    mPrefList.emplace_back("rotationSpeed", "1.0f", "camera");
    mPrefList.emplace_back("zoomSpeed", "1.5f", "camera");
}

void CPrefs::SavePrefs(void) const
{
    json data;
    std::ofstream file("Data/preferences.json", std::ios::out);

    Printf("[CPrefs::SavePrefs] saving preferences...");

    // general configuration
    {
        data["config"]["enginePath"] = FindPref("enginePath");
        data["config"]["exePath"] = FindPref("exePath");
    }
    // graphics configuration
    {
        data["graphics"]["textureDetail"] = FindPref("textureDetail");
        data["graphics"]["textureFiltering"] = FindPref("textureFiltering");
    }
    // camera configration
    {
        data["camera"]["moveSpeed"] = FindPref("moveSpeed");
        data["camera"]["rotationSpeed"] = FindPref("rotationSpeed");
        data["camera"]["zoomSpeed"] = FindPref("zoomSpeed");
    }
    if (!file.is_open()) {
        Error("[CPrefs::SavePrefs] failed to open preferences file in save mode");
    }
    file.width(4);
    file << data;
    file.close();
}

CGameConfig::CGameConfig(void)
{
    Cmd_AddCommand("preflist", PrintPrefs_f);

    Printf("[CGameConfig::Init] initializing current configuration");
    mEditorPath = pwdString.string() + "/Data/";

    mPrefs.LoadPrefs("Data/preferences.json");

    mEnginePath = mPrefs["enginePath"];
    mExecutablePath = mPrefs["exePath"];

    mTextureDetail = StringToInt(mPrefs["textureDetail"], texture_details, arraylen(texture_details));
    mTextureFiltering = StringToInt(mPrefs["textureFiltering"], texture_filters, arraylen(texture_filters));

    mCameraMoveSpeed = atof(mPrefs["moveSpeed"].c_str());
    mCameraRotationSpeed = atof(mPrefs["rotationSpeed"].c_str());
    mCameraZoomSpeed = atof(mPrefs["zoomSpeed"].c_str());
    
    if (mEnginePath.find_last_of(GLNOMAD_ENGINE) != std::string::npos) {
        mEngineName = GLNOMAD_ENGINE;
    }
    else {
        mEngineName = "Unknown";
    }
}

CGameConfig::~CGameConfig()
{
    mPrefs.SavePrefs();
}
