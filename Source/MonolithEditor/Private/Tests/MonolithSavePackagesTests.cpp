#if WITH_DEV_AUTOMATION_TESTS

#include "MonolithAssetUtils.h"
#include "MonolithEditorActions.h"
#include "Curves/CurveFloat.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MonolithSavePackagesTests
{
	struct FFixture
	{
		FString Root = TEXT("/MonolithSaveGuard_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
		FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/MonolithSaveGuard") / FGuid::NewGuid().ToString(EGuidFormats::Digits)) + TEXT("/");
		FString Filename = Directory + TEXT("Asset.uasset");
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<UCurveFloat> Asset;

		FFixture()
		{
			IFileManager::Get().MakeDirectory(*Directory, true);
			FPackageName::RegisterMountPoint(Root, Directory);
			Package.Reset(CreatePackage(*(Root + TEXT("Asset"))));
			Asset.Reset(NewObject<UCurveFloat>(Package.Get(), TEXT("Asset"), RF_Public | RF_Standalone));
			Package->SetDirtyFlag(true);
		}

		~FFixture()
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false);
			IFileManager::Get().Delete(*Filename);
			FPackageName::UnRegisterMountPoint(Root, Directory);
			Asset->ClearFlags(RF_Standalone);
			Package->SetDirtyFlag(false);
			IFileManager::Get().DeleteDirectory(*Directory);
		}

		FMonolithActionResult Save(bool bDryRun = false) const
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetArrayField(TEXT("packages"), { MakeShared<FJsonValueString>(Package->GetName()) });
			Params->SetBoolField(TEXT("dry_run"), bDryRun);
			return FMonolithEditorActions::HandleSavePackages(Params);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonolithSaveReadOnlyPackageTest, "Monolith.Editor.SavePackages.ReadOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonolithSaveReadOnlyPackageTest::RunTest(const FString& Parameters)
{
	MonolithSavePackagesTests::FFixture Fixture;
	const FString Original = TEXT("Read-only sentinel: this file must not be overwritten");
	if (!TestTrue(TEXT("Create read-only fixture"), FFileHelper::SaveStringToFile(Original, *Fixture.Filename)) ||
			!TestTrue(TEXT("Set read-only attribute"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Fixture.Filename, true)))
	{
		return false;
	}

	FString Filename;
	FString Error;
	TestFalse(TEXT("Shared Blueprint save guard rejects read-only file"), FMonolithAssetUtils::CanSavePackage(Fixture.Package.Get(), Filename, Error));
	TestTrue(TEXT("Shared guard explains read-only rejection"), Error.Contains(TEXT("read-only")));

	for (bool bDryRun : { false, true })
	{
		const FMonolithActionResult Result = Fixture.Save(bDryRun);
		if (!TestTrue(TEXT("Per-package result returned without invoking an engine save"), Result.bSuccess && Result.Result.IsValid()))
		{
			return false;
		}
		const TSharedPtr<FJsonObject> Row = Result.Result->GetArrayField(TEXT("results"))[0]->AsObject();
		TestTrue(TEXT("Read-only target reported"), Row->GetBoolField(TEXT("read_only")));
		if (bDryRun)
		{
			TestTrue(TEXT("Dry run preserves checkout-aware save intent"), Row->GetBoolField(TEXT("would_save")));
		}
		else
		{
			TestFalse(TEXT("Read-only request reports failure without checkout"), Result.Result->GetBoolField(TEXT("ok")));
			TestFalse(TEXT("Package not saved"), Row->GetBoolField(TEXT("saved")));
			TestTrue(TEXT("Read-only reason returned"), Row->GetStringField(TEXT("error")).Contains(TEXT("read-only")));
		}
	}

	FString After;
	TestTrue(TEXT("Read sentinel after blocked saves"), FFileHelper::LoadFileToString(After, *Fixture.Filename));
	TestEqual(TEXT("Original file unchanged"), After, Original);
	TestTrue(TEXT("Read-only attribute retained"), IFileManager::Get().IsReadOnly(*Fixture.Filename));
	TestTrue(TEXT("In-memory changes remain dirty"), Fixture.Package->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonolithSaveWritablePackageTest, "Monolith.Editor.SavePackages.Writable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonolithSaveWritablePackageTest::RunTest(const FString& Parameters)
{
	MonolithSavePackagesTests::FFixture Fixture;
	FString Filename;
	FString Error;
	TestTrue(TEXT("New writable destination allowed"), FMonolithAssetUtils::CanSavePackage(Fixture.Package.Get(), Filename, Error));
	const FMonolithActionResult Result = Fixture.Save(true);
	if (!TestTrue(TEXT("Writable package accepted by dry run"), Result.bSuccess && Result.Result.IsValid() && Result.Result->GetBoolField(TEXT("ok"))))
	{
		return false;
	}
	TestTrue(TEXT("Dry run would save dirty writable package"), Result.Result->GetArrayField(TEXT("results"))[0]->AsObject()->GetBoolField(TEXT("would_save")));
	TestFalse(TEXT("Dry run does not write an asset"), IFileManager::Get().FileExists(*Fixture.Filename));
	TestFalse(TEXT("Transient package rejected"), FMonolithAssetUtils::CanSavePackage(GetTransientPackage(), Filename, Error));
	TestFalse(TEXT("Null package rejected"), FMonolithAssetUtils::CanSavePackage(nullptr, Filename, Error));
	return true;
}

#endif
