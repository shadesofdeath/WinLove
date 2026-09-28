#pragma once
// Our own declarations of the DISM API subset WinLove uses, loaded at runtime from
// %SystemRoot%\System32\dismapi.dll. The official header ships only with the ADK; declaring it
// here keeps the build free of ADK (D-017). Layouts copied from dismapi.h (ADK 10.1.26100),
// which packs its structs to 1 byte.
#include <windows.h>

namespace wl::core::dismapi {

using Session = UINT;

enum LogLevel : int { LogErrors = 0, LogErrorsWarnings, LogErrorsWarningsInfo };
enum ImageIdentifier : int { ImageIndex = 0, ImageName };
enum MountMode : int { ReadWrite = 0, ReadOnly };
enum MountStatus : int { MountOk = 0, MountNeedsRemount, MountInvalid };
enum PackageIdentifier : int { PackageNone = 0, PackageName, PackagePath };
enum PackageFeatureState : int {
    StateNotPresent = 0,
    StateUninstallPending,
    StateStaged,
    StateRemoved, // "Resolved" in the header, same value
    StateInstalled,
    StateInstallPending,
    StateSuperseded,
    StatePartiallyInstalled,
};
enum ReleaseType : int {
    ReleaseCriticalUpdate = 0, ReleaseDriver, ReleaseFeaturePack, ReleaseHotfix, ReleaseSecurityUpdate,
    ReleaseSoftwareUpdate, ReleaseUpdate, ReleaseUpdateRollup, ReleaseLanguagePack, ReleaseFoundation,
    ReleaseServicePack, ReleaseProduct, ReleaseLocalPack, ReleaseOther, ReleaseOnDemandPack,
};

inline constexpr DWORD kMountReadWrite = 0x0;
inline constexpr DWORD kMountReadOnly = 0x1;
inline constexpr DWORD kCommitImage = 0x0;
inline constexpr DWORD kDiscardImage = 0x1;

#pragma pack(push, 1)
struct String {
    PCWSTR value;
};
struct MountedImageInfo {
    PCWSTR mountPath;
    PCWSTR imageFilePath;
    UINT imageIndex;
    MountMode mountMode;
    MountStatus mountStatus;
};
struct Package {
    PCWSTR packageName;
    PackageFeatureState state;
    ReleaseType releaseType;
    SYSTEMTIME installTime;
};
struct Feature {
    PCWSTR featureName;
    PackageFeatureState state;
};
struct Capability {
    PCWSTR name;
    PackageFeatureState state;
};
enum RestartType : int { RestartNo = 0, RestartPossible, RestartRequired };
struct CustomProperty {
    PCWSTR name;
    PCWSTR value;
    PCWSTR path;
};
struct FeatureInfo {
    PCWSTR featureName;
    PackageFeatureState featureState;
    PCWSTR displayName;
    PCWSTR description;
    RestartType restartRequired;
    CustomProperty* customProperty;
    UINT customPropertyCount;
};
struct CapabilityDetail {
    PCWSTR name;
    PackageFeatureState state;
    PCWSTR displayName;
    PCWSTR description;
    UINT downloadSize;
    UINT installSize;
};
#pragma pack(pop)

using ProgressCallback = void(CALLBACK*)(UINT current, UINT total, PVOID userData);

// Function table; null members mean the entry point is missing from this dismapi.dll.
struct Api {
    HRESULT(WINAPI* initialize)(LogLevel, PCWSTR logFile, PCWSTR scratchDirectory) = nullptr;
    HRESULT(WINAPI* shutdown)() = nullptr;
    HRESULT(WINAPI* mountImage)(PCWSTR image, PCWSTR mountPath, UINT index, PCWSTR name, ImageIdentifier,
                                DWORD flags, HANDLE cancel, ProgressCallback, PVOID) = nullptr;
    HRESULT(WINAPI* unmountImage)(PCWSTR mountPath, DWORD flags, HANDLE cancel, ProgressCallback, PVOID) = nullptr;
    HRESULT(WINAPI* openSession)(PCWSTR imagePath, PCWSTR windowsDirectory, PCWSTR systemDrive, Session*) = nullptr;
    HRESULT(WINAPI* closeSession)(Session) = nullptr;
    HRESULT(WINAPI* getLastErrorMessage)(String**) = nullptr;
    HRESULT(WINAPI* getMountedImageInfo)(MountedImageInfo**, UINT*) = nullptr;
    HRESULT(WINAPI* cleanupMountpoints)() = nullptr;
    HRESULT(WINAPI* remountImage)(PCWSTR mountPath) = nullptr;
    HRESULT(WINAPI* deleteStructure)(void*) = nullptr;
    HRESULT(WINAPI* getPackages)(Session, Package**, UINT*) = nullptr;
    HRESULT(WINAPI* getFeatures)(Session, PCWSTR identifier, PackageIdentifier, Feature**, UINT*) = nullptr;
    HRESULT(WINAPI* getCapabilities)(Session, Capability**, UINT*) = nullptr;
    HRESULT(WINAPI* getFeatureInfo)(Session, PCWSTR feature, PCWSTR identifier, PackageIdentifier, FeatureInfo**) = nullptr;
    HRESULT(WINAPI* getCapabilityInfo)(Session, PCWSTR name, CapabilityDetail**) = nullptr;
    HRESULT(WINAPI* disableFeature)(Session, PCWSTR feature, PCWSTR package, BOOL removePayload, HANDLE cancel,
                                    ProgressCallback, PVOID) = nullptr;
    HRESULT(WINAPI* enableFeature)(Session, PCWSTR feature, PCWSTR identifier, PackageIdentifier, BOOL limitAccess,
                                   PCWSTR* sourcePaths, UINT sourceCount, BOOL enableAll, HANDLE cancel,
                                   ProgressCallback, PVOID) = nullptr;
    HRESULT(WINAPI* removePackage)(Session, PCWSTR identifier, PackageIdentifier, HANDLE cancel, ProgressCallback,
                                   PVOID) = nullptr;
    HRESULT(WINAPI* removeCapability)(Session, PCWSTR name, HANDLE cancel, ProgressCallback, PVOID) = nullptr;
};

} // namespace wl::core::dismapi
