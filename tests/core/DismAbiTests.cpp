// Compile-time proof that our DISM declarations (core/image/dism/DismApi.h, D-017) match the
// official ADK dismapi.h byte for byte. Built only when the ADK header is installed; a layout or
// value mismatch breaks the build.
#include "core/image/dism/DismApi.h"

#define DISMAPI_LATEST
#include <dismapi.h>

#include <doctest.h>

#include <cstddef>

namespace d = wl::core::dismapi;

#define WL_SAME_LAYOUT(ours, theirs) static_assert(sizeof(ours) == sizeof(theirs), #ours " size differs from " #theirs)
#define WL_SAME_OFFSET(ours, a, theirs, b)                                                                              \
    static_assert(offsetof(ours, a) == offsetof(theirs, b), #ours "::" #a " offset differs from " #theirs "::" #b)

WL_SAME_LAYOUT(d::Session, DismSession);
WL_SAME_LAYOUT(d::PackageFeatureState, DismPackageFeatureState);
WL_SAME_LAYOUT(d::MountStatus, DismMountStatus);

WL_SAME_LAYOUT(d::String, DismString);
WL_SAME_LAYOUT(d::MountedImageInfo, DismMountedImageInfo);
WL_SAME_OFFSET(d::MountedImageInfo, imageFilePath, DismMountedImageInfo, ImageFilePath);
WL_SAME_OFFSET(d::MountedImageInfo, imageIndex, DismMountedImageInfo, ImageIndex);
WL_SAME_OFFSET(d::MountedImageInfo, mountMode, DismMountedImageInfo, MountMode);
WL_SAME_OFFSET(d::MountedImageInfo, mountStatus, DismMountedImageInfo, MountStatus);

WL_SAME_LAYOUT(d::Package, DismPackage);
WL_SAME_OFFSET(d::Package, state, DismPackage, PackageState);
WL_SAME_OFFSET(d::Package, releaseType, DismPackage, ReleaseType);
WL_SAME_OFFSET(d::Package, installTime, DismPackage, InstallTime);

WL_SAME_LAYOUT(d::Feature, DismFeature);
WL_SAME_OFFSET(d::Feature, state, DismFeature, State);
WL_SAME_LAYOUT(d::Capability, DismCapability);
WL_SAME_OFFSET(d::Capability, state, DismCapability, State);
WL_SAME_LAYOUT(d::CustomProperty, DismCustomProperty);

WL_SAME_LAYOUT(d::FeatureInfo, DismFeatureInfo);
WL_SAME_OFFSET(d::FeatureInfo, featureState, DismFeatureInfo, FeatureState);
WL_SAME_OFFSET(d::FeatureInfo, displayName, DismFeatureInfo, DisplayName);
WL_SAME_OFFSET(d::FeatureInfo, description, DismFeatureInfo, Description);
WL_SAME_OFFSET(d::FeatureInfo, restartRequired, DismFeatureInfo, RestartRequired);
WL_SAME_OFFSET(d::FeatureInfo, customProperty, DismFeatureInfo, CustomProperty);
WL_SAME_OFFSET(d::FeatureInfo, customPropertyCount, DismFeatureInfo, CustomPropertyCount);

WL_SAME_LAYOUT(d::AppxPackage, DismAppxPackage);
WL_SAME_OFFSET(d::AppxPackage, displayName, DismAppxPackage, DisplayName);
WL_SAME_OFFSET(d::AppxPackage, majorVersion, DismAppxPackage, MajorVersion);
WL_SAME_OFFSET(d::AppxPackage, architecture, DismAppxPackage, Architecture);
WL_SAME_OFFSET(d::AppxPackage, installLocation, DismAppxPackage, InstallLocation);
WL_SAME_OFFSET(d::AppxPackage, region, DismAppxPackage, Region);

WL_SAME_LAYOUT(d::CapabilityDetail, DismCapabilityInfo);
WL_SAME_OFFSET(d::CapabilityDetail, state, DismCapabilityInfo, State);
WL_SAME_OFFSET(d::CapabilityDetail, displayName, DismCapabilityInfo, DisplayName);
WL_SAME_OFFSET(d::CapabilityDetail, description, DismCapabilityInfo, Description);
WL_SAME_OFFSET(d::CapabilityDetail, downloadSize, DismCapabilityInfo, DownloadSize);
WL_SAME_OFFSET(d::CapabilityDetail, installSize, DismCapabilityInfo, InstallSize);

WL_SAME_LAYOUT(d::DriverPackage, DismDriverPackage);
WL_SAME_OFFSET(d::DriverPackage, originalFileName, DismDriverPackage, OriginalFileName);
WL_SAME_OFFSET(d::DriverPackage, inBox, DismDriverPackage, InBox);
WL_SAME_OFFSET(d::DriverPackage, catalogFile, DismDriverPackage, CatalogFile);
WL_SAME_OFFSET(d::DriverPackage, classDescription, DismDriverPackage, ClassDescription);
WL_SAME_OFFSET(d::DriverPackage, bootCritical, DismDriverPackage, BootCritical);
WL_SAME_OFFSET(d::DriverPackage, driverSignature, DismDriverPackage, DriverSignature);
WL_SAME_OFFSET(d::DriverPackage, date, DismDriverPackage, Date);
WL_SAME_OFFSET(d::DriverPackage, majorVersion, DismDriverPackage, MajorVersion);
WL_SAME_OFFSET(d::DriverPackage, revision, DismDriverPackage, Revision);

// Enum values and flags (compared as integers: the enum types differ by design).
constexpr bool eq(auto ours, auto theirs) {
    return static_cast<long long>(ours) == static_cast<long long>(theirs);
}
static_assert(eq(d::StateNotPresent, DismStateNotPresent) && eq(d::StateStaged, DismStateStaged) &&
              eq(d::StateRemoved, DismStateRemoved) && eq(d::StateInstalled, DismStateInstalled) &&
              eq(d::StateInstallPending, DismStateInstallPending) && eq(d::StatePartiallyInstalled, DismStatePartiallyInstalled));
static_assert(eq(d::MountOk, DismMountStatusOk) && eq(d::MountNeedsRemount, DismMountStatusNeedsRemount) &&
              eq(d::MountInvalid, DismMountStatusInvalid));
static_assert(eq(d::ReadWrite, DismReadWrite) && eq(d::ReadOnly, DismReadOnly));
static_assert(eq(d::PackageNone, DismPackageNone) && eq(d::PackageName, DismPackageName) && eq(d::PackagePath, DismPackagePath));
static_assert(eq(d::RestartNo, DismRestartNo) && eq(d::RestartRequired, DismRestartRequired));
static_assert(eq(d::kMountReadWrite, DISM_MOUNT_READWRITE) && eq(d::kMountReadOnly, DISM_MOUNT_READONLY) &&
              eq(d::kCommitImage, DISM_COMMIT_IMAGE) && eq(d::kDiscardImage, DISM_DISCARD_IMAGE));
static_assert(eq(d::LogErrorsWarningsInfo, DismLogErrorsWarningsInfo));
static_assert(eq(d::SignatureUnknown, DismDriverSignatureUnknown) && eq(d::SignatureUnsigned, DismDriverSignatureUnsigned) &&
              eq(d::SignatureSigned, DismDriverSignatureSigned));

TEST_CASE("DISM declarations match the ADK dismapi.h (checked at compile time)") {
    CHECK(sizeof(d::FeatureInfo) == sizeof(DismFeatureInfo));
}
