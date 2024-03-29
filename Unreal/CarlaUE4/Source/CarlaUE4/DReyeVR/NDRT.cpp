#include "EgoVehicle.h"
#include "Carla/Game/CarlaStatics.h"	// GetCurrentEpisode
#include "Engine/EngineTypes.h"			// EBlendMode
#include "GameFramework/Actor.h"		// Destroy
#include "UObject/ConstructorHelpers.h" // ConstructorHelpers
#include "MediaPlayer.h"
#include "FileMediaSource.h"
#include "MediaTexture.h"
#include "MediaSoundComponent.h"
#include "NBackProgressBar.h"
#include "WidgetComponent.h"

void AEgoVehicle::SetupNDRT()
{
	// Construct the head-up display
	ConstructHUD();

	// Construct the HUD Debugger (Will set blank strings)
	ConstructHUDDebugger();

	// Present the visual elements based on the task type {n-back, TV show, etc..}
	switch (CurrTaskType)
	{
	case TaskType::NBackTask:
		ConstructNBackElements();
		SetHUDTimeThreshold(GeneralParams.Get<float>("EyeTracker", "GazeOnHUDTimeConstraint")); // Setting time constraint based as the average of the n-back task time limits
		break;
	case TaskType::TVShowTask:
		ConstructTVShowElements();
		break;
	}
}

void AEgoVehicle::StartNDRT()
{
	// Start the NDRT based on the task type
	switch (CurrTaskType)
	{
	case TaskType::NBackTask:
		// We must set the n-back task title (outside of the constructor call; hence done here)
		SetNBackTitle(static_cast<int>(CurrentNValue));

		// Figuring out the total n-back tasks that will be run
		// Note: OneBackTimeLimit * 80 = 3 * 80 = 240 seconds; There will be 80, 60, and 48 1-back, 2-back, and 3-back tasks
		TotalNBackTasks = static_cast<int32>((OneBackTimeLimit * 80) / (OneBackTimeLimit + 1.0 * (static_cast<int>(CurrentNValue) - 1)));
		// Add randomly generated elements to NBackPrompts
		for (int32 i = 0; i < TotalNBackTasks; i++)
		{
			// NOTE: A "MATCH" is generated 50% of the times
			FString SingleLetter;
			if (FMath::RandBool() && i >= static_cast<int>(CurrentNValue))
			{
				SingleLetter = NBackPrompts[i - static_cast<int>(CurrentNValue)];
			}
			else
			{
				TCHAR RandomChar = FMath::RandRange('A', 'Z');
				SingleLetter = FString(1, &RandomChar);
			}
			NBackPrompts.Add(SingleLetter);
		}
		// Set the first index letter on the HUD
		SetLetter(NBackPrompts[0]);
		// Set the starting time stamp of the n-back task trial now
		NBackTrialStartTimestamp = FPlatformTime::Seconds();
		break;
	case TaskType::TVShowTask:
		// Retrieve the media player material and the video source which will be used later to play the video
		MediaPlayerMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, TEXT("Material'/Game/NDRT/TVShow/MediaPlayer/M_MediaPlayer.M_MediaPlayer'")));
		MediaPlayerSource = Cast<UFileMediaSource>(StaticLoadObject(UFileMediaSource::StaticClass(), nullptr, TEXT("FileMediaSource'/Game/NDRT/TVShow/MediaPlayer/FileMediaSource.FileMediaSource'")));
		// Change the static mesh material to media player material
		MediaPlayerMesh->SetMaterial(0, MediaPlayerMaterial);
		// Retrieve the source of the video and play it
		MediaPlayer->OpenSource(MediaPlayerSource);
		break;
	}

	// Initially, hide the NDRT and toggle it when appropriate
	ToggleNDRT(false);
}

void AEgoVehicle::ToggleNDRT(bool active)
{
	// Make all the HUD meshes appear/disappear
	PrimaryHUD->SetVisibility(active, false);
	// We only need to make them disappear. We don't need to make them visible when NDRT interaction is enabled again.
	if (!active)
	{
		SecondaryHUD->SetVisibility(active, false);
		DisableHUD->SetVisibility(active, false);
	}

	// Make all the NDRT-relevant elements appear/disappear
	switch (CurrTaskType)
	{
	case TaskType::NBackTask:
		NBackLetter->SetVisibility(active, false);
		NBackControlsInfo->SetVisibility(active, false);
		NBackTitle->SetVisibility(active, false);
		ProgressWidgetComponent->SetVisibility(active, false);
		break;
	case TaskType::TVShowTask:
		MediaPlayerMesh->SetVisibility(active, false);
		break;
	default:
		break;
	}
}

void AEgoVehicle::ToggleAlertOnNDRT(bool active)
{
	if (active)
	{
		// Make a red rim appear around the HUD
		SecondaryHUD->SetVisibility(true, false);
		// Play a subtle alert sound if not already played
		if (!bIsAlertOnNDRTOn)
		{
			// Play an alert sound once (looping is disabled)
			HUDAlertSound->Play();
			bIsAlertOnNDRTOn = true;
		}
	}
	else
	{
		// Make the red rim around the HUD disappear
		SecondaryHUD->SetVisibility(false, false);
		bIsAlertOnNDRTOn = false;
	}
}

void AEgoVehicle::SetInteractivityOfNDRT(bool interactivity)
{
	// If interactivity is true, set visibility false, otherwise true.
	DisableHUD->SetVisibility(!interactivity, false);
}

void AEgoVehicle::TerminateNDRT()
{
	// This method assumes that whensoever its called, the trial is over
	// NOTE: Change code structure if that is not intended behaviour
	SetMessagePaneText(TEXT("Trial Over"), FColor::Black);

	// Set the vehicle status to TrialOver, so that client can execute respective behaviour
	UpdateVehicleStatus(VehicleStatus::TrialOver);

	// Save all the NDRT performance data here if needed

	// This is done so that the updated file (by python API) is re-read
	ExperimentParams = ConfigFile(FPaths::Combine(CarlaUE4Path, TEXT("Config/ExperimentConfig.ini")));

	// Preparing the common row data
	TArray<FString> CommonRowData = { ExperimentParams.Get<FString>("General", "ParticipantID") };
	const FString CurrentBlock = ExperimentParams.Get<FString>("General", "CurrentBlock");
	CommonRowData.Add(CurrentBlock.Mid(0, 6)); // Add the block number
	CommonRowData.Add(CurrentBlock.Mid(6, 6)); // Add the trial number
	CommonRowData.Add(ExperimentParams.Get<FString>(CurrentBlock, "NDRTTaskType"));
	CommonRowData.Add(ExperimentParams.Get<FString>(CurrentBlock, "TaskSetting"));
	CommonRowData.Add(ExperimentParams.Get<FString>(CurrentBlock, "Traffic"));

	if (CurrTaskType == TaskType::NBackTask)
	{
		// Define the CSV file path
		const FString NBackCSVFilePath = FPaths::ProjectContentDir() / TEXT("NDRTPerformance/n_back.csv");

		// Check if the file exists, and if not, create it and write the header
		if (!FPaths::FileExists(NBackCSVFilePath))
		{
			FString Header = TEXT("ParticipantID,BlockNumber,TrialNumber,TaskType,TaskSetting,TrafficComplexity,Timestamp,NBackPrompt,NBackResponse\n");
			FFileHelper::SaveStringToFile(Header, *NBackCSVFilePath);
		}

		// Now, iterate through all the NBack prompts and responses and write in the CSV file
		check(NBackPrompts.Num() == NBackRecordedResponses.Num() && NBackPrompts.Num() == NBackResponseTimestamp.Num())

		for (int32 i = 0; i < NBackPrompts.Num(); i++)
		{
			// Combine the common data with the specific data for this iteration
			TArray<FString> NBackRowData = CommonRowData;
			NBackRowData.Add(NBackResponseTimestamp[i]);
			NBackRowData.Add(NBackPrompts[i]);
			NBackRowData.Add(NBackRecordedResponses[i]);

			// Convert the row data to a single comma-separated string
			FString RowString = FString::Join(NBackRowData, TEXT(","));

			// Append this row to the CSV file
			FFileHelper::SaveStringToFile(RowString + TEXT("\n"), *NBackCSVFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
		}
	}

	// Lastly, add non-NDRT specific data.
	// (1) Adding the number of interruption alerts issued

	// Define the CSV file path
	const FString InterCSVFilePath = FPaths::ProjectContentDir() / TEXT("SystemData/interruption_freq.csv");

	// Check if the file exists, and if not, create it and write the header
	if (!FPaths::FileExists(InterCSVFilePath))
	{
		FString Header = TEXT("ParticipantID,BlockNumber,TrialNumber,TaskType,TaskSetting,TrafficComplexity,InterruptionFrequency\n");
		FFileHelper::SaveStringToFile(Header, *InterCSVFilePath);
	}

	// Combine the common data with the specific data for frequency
	TArray<FString> InterRowData = CommonRowData;
	InterRowData.Add(FString::FromInt(InterruptionAlertFrequency));

	// Convert the row data to a single comma-separated string
	FString InterRowString = FString::Join(InterRowData, TEXT(","));

	// Append this row to the CSV file
	FFileHelper::SaveStringToFile(InterRowString + TEXT("\n"), *InterCSVFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

void AEgoVehicle::TickNDRT()
{
	// WARNING/NOTE: It is the responsibility of the respective NDRT tick methods to change the vehicle status
	// to TrialOver when the NDRT task is over.

	// Calling the HUD Gaze timer calculator just to be sure it is called frequently enough
	GazeOnHUDTime();

	// Create a lambda function to call the tick method based on the NDRT set from configuration file
	auto HandleTaskTick = [&]()
	{
		switch (CurrTaskType)
		{
		case TaskType::NBackTask:
			NBackTaskTick();
			break;
		case TaskType::TVShowTask:
			TVShowTaskTick();
			break;
		default:
			break;
		}
	};

	// CASE: When NDRT engagement is disabled/not expected (during TORs or manual control)
	if (!IsSkippingSR && (CurrVehicleStatus == VehicleStatus::ManualDrive || CurrVehicleStatus == VehicleStatus::TrialOver))
	{
		if (CurrVehicleStatus == VehicleStatus::ManualDrive)
		{
			SetMessagePaneText(TEXT("Manual Drive"), FColor::Black);
		}

		// This is the case when scenario runner has not kicked in. Just do nothing
		// This means not allowing driver to interact with NDRT
		return; // Exit for efficiency
	}

	// CASE: During a take-over request, disable and hide the NDRT interface
	if (CurrVehicleStatus == VehicleStatus::TakeOver || CurrVehicleStatus == VehicleStatus::TakeOverManual)
	{
		// Disable the NDRT interface by toggling it
		ToggleNDRT(false);

		// Show a message asking the driver to take control of the vehicle
		SetMessagePaneText(TEXT("Take Over!"), FColor::Red);

		// Play the TOR alert sound if not already
		if (CurrVehicleStatus == VehicleStatus::TakeOver && !bIsTORAlertPlaying)
		{
			TORAlertSound->Play();
			bIsTORAlertPlaying = true;
		}
		else if (CurrVehicleStatus == VehicleStatus::TakeOverManual && bIsTORAlertPlaying)
		{
			TORAlertSound->Stop();
			bIsTORAlertPlaying = false;
		}

		return; // Exit for efficiency
	}

	// Now, we are dealing with all the cases when NDRT task engagement is expected.
	// CASE: This is the test trial condition. In this case, the scenario runner is not executed, and the
	// participant is only expected to engage in the NDRT task. Vehicle status is also irrelevant
	if (IsSkippingSR)
	{
		// Allow the driver to engage in NDRT without interruption to practice the task.
		SetMessagePaneText(TEXT("Test Trial"), FColor::Black);
		ToggleNDRT(true);
		HandleTaskTick();
		return; // Exit for efficiency
	}

	// CASE: Execute necessary UI behaviour/messages for when autopilot is engaged.
	if (CurrVehicleStatus == VehicleStatus::Autopilot)
	{
		// Tell the driver that the ADS is engaged
		SetMessagePaneText(TEXT("Autopilot Engaged"), FColor::Green);

		// Make sure NDRT is toggled up again
		if (AutopilotStartTimestamp > 0.f)
		{
			if ((UGameplayStatics::GetRealTimeSeconds(GetWorld()) - AutopilotStartTimestamp) >= NDRTStartLag)
			{
				ToggleNDRT(true);
				HandleTaskTick();
			}
		}
		else
		{
			AutopilotStartTimestamp = UGameplayStatics::GetRealTimeSeconds(GetWorld());
		}

		return; // Exit for efficiency
	}

	if (CurrVehicleStatus == VehicleStatus::ResumedAutopilot)
	{
		// Tell the driver that the ADS is engaged
		SetMessagePaneText(TEXT("Autopilot Engaged"), FColor::Green);

		// Make sure NDRT is toggled up again
		if (ResumedAutopilotStartTimestamp > 0.f)
		{
			if ((UGameplayStatics::GetRealTimeSeconds(GetWorld()) - ResumedAutopilotStartTimestamp) >= NDRTStartLag)
			{
				ToggleNDRT(true);
				HandleTaskTick();
			}
		}
		else
		{
			ResumedAutopilotStartTimestamp = UGameplayStatics::GetRealTimeSeconds(GetWorld());
		}

		return; // Exit for efficiency
	}

	// During pre alert, call the HandleTaskTick()
	HandleTaskTick();

	// During the pre-alert period, allow NDRT engagement, but with potential restriction based
	// on the interruption paradigm. However, the task (timer) will continue to run
	if (CurrVehicleStatus == VehicleStatus::PreAlertAutopilot)
	{
		// Show a pre-alert message suggesting that a take-over request may be issued in the future
		SetMessagePaneText(TEXT("Prepare to Take Over"), FColor::Orange);

		// Also give a audio notification to make sure they see it
		if (!bIsPreAlertOn)
		{
			PreAlertSound->Play();
			bIsPreAlertOn = true;
		}
	}

	if (GazeOnHUDTime() >= GazeOnHUDTimeConstraint)
	{
		switch (CurrInterruptionParadigm)
		{
		case InterruptionParadigm::SystemRecommended:
			ToggleAlertOnNDRT(true);
			if (!bIsAlertFreqCounted)
			{
				InterruptionAlertFrequency++;
				bIsAlertFreqCounted = true;
			}
			break;

		case InterruptionParadigm::SystemInitiated:
			ToggleAlertOnNDRT(true);
			SetInteractivityOfNDRT(false);
			if (!bIsAlertFreqCounted)
			{
				InterruptionAlertFrequency++;
				bIsAlertFreqCounted = true;
			}
			break;
		default:
			break;
		}
	}
	else
	{
		switch (CurrInterruptionParadigm)
		{
		case InterruptionParadigm::SystemRecommended:
			ToggleAlertOnNDRT(false);
			bIsAlertFreqCounted = false;
			break;

		case InterruptionParadigm::SystemInitiated:
			ToggleAlertOnNDRT(false);
			SetInteractivityOfNDRT(true);
			bIsAlertFreqCounted = false;
			break;

		default:
			break;
		}
	}
}

void AEgoVehicle::ConstructHUD()
{
	// Creating the primary head-up display to display the non-driving related task
	PrimaryHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Primary HUD"));
	PrimaryHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	PrimaryHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrimaryHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "PrimaryHUDLocation"));
	FString PathToMeshPHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_PrimaryHUD.SM_PrimaryHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> PHUDMeshObj(*PathToMeshPHUD);
	PrimaryHUD->SetStaticMesh(PHUDMeshObj.Object);
	PrimaryHUD->SetCastShadow(false);

	// Creating the secondary head-up display which will give the notification to switch task.
	SecondaryHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Secondary HUD"));
	SecondaryHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	SecondaryHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SecondaryHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "SecondaryHUDLocation"));
	FString PathToMeshSHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_SecondaryHUD.SM_SecondaryHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> SHUDMeshObj(*PathToMeshSHUD);
	SecondaryHUD->SetStaticMesh(SHUDMeshObj.Object);
	SecondaryHUD->SetCastShadow(false);
	SecondaryHUD->SetVisibility(false, false); // Set it hidden by default, and only make it appear when alerting.

	// Creating the disabling head-up display for the system-initiated task switching
	DisableHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disable HUD"));
	DisableHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	DisableHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisableHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "DisableHUDLocation"));
	FString PathToMeshDHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_DisableHUD.SM_DisableHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> DHUDMeshObj(*PathToMeshDHUD);
	DisableHUD->SetStaticMesh(DHUDMeshObj.Object);
	DisableHUD->SetCastShadow(false);
	DisableHUD->SetVisibility(false, false); // Set it hidden by default, and only make it appear when alerting.

	// Construct the message pane here
	MessagePane = CreateEgoObject<UTextRenderComponent>("MessagePane");
	MessagePane->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	MessagePane->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "MessagePaneLocation"));
	MessagePane->SetTextRenderColor(FColor::Black);
	MessagePane->SetText(FText::FromString(""));
	MessagePane->SetXScale(1.f);
	MessagePane->SetYScale(1.f);
	MessagePane->SetWorldSize(7); // scale the font with this
	MessagePane->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	MessagePane->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);

	// Also construct all the sounds here
	static ConstructorHelpers::FObjectFinder<USoundWave> HUDAlertSoundWave(
		TEXT("SoundWave'/Game/DReyeVR/EgoVehicle/Extra/InterruptionSound.InterruptionSound'"));
	HUDAlertSound = CreateDefaultSubobject<UAudioComponent>(TEXT("HUDAlert"));
	HUDAlertSound->SetupAttachment(GetRootComponent());
	HUDAlertSound->bAutoActivate = false;
	HUDAlertSound->SetSound(HUDAlertSoundWave.Object);

	static ConstructorHelpers::FObjectFinder<USoundWave> PreAlertSoundWave(
		TEXT("SoundWave'/Game/DReyeVR/EgoVehicle/Extra/PreAlertSound.PreAlertSound'"));
	PreAlertSound = CreateDefaultSubobject<UAudioComponent>(TEXT("PreAlert"));
	PreAlertSound->SetupAttachment(GetRootComponent());
	PreAlertSound->bAutoActivate = false;
	PreAlertSound->SetSound(PreAlertSoundWave.Object);

	static ConstructorHelpers::FObjectFinder<USoundWave> TORAlertSoundWave(
		TEXT("SoundWave'/Game/DReyeVR/EgoVehicle/Extra/TORAlertSound.TORAlertSound'"));
	TORAlertSound = CreateDefaultSubobject<UAudioComponent>(TEXT("TORAlert"));
	TORAlertSound->SetupAttachment(GetRootComponent());
	TORAlertSound->bAutoActivate = false;
	TORAlertSound->SetSound(TORAlertSoundWave.Object);
}

void AEgoVehicle::ConstructNBackElements()
{
	// Creating the letter pane to show letters for the n-back task
	NBackLetter = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Letter Pane"));
	NBackLetter->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackLetter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackLetter->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "LetterLocation"));
	FString PathToMeshNBackLetter = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_LetterPane.SM_LetterPane'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackLetterMeshObj(*PathToMeshNBackLetter);
	NBackLetter->SetStaticMesh(NBackLetterMeshObj.Object);
	NBackLetter->SetCastShadow(false);

	// Creating a pane to show controls on the logitech steering wheel for the n-back task
	NBackControlsInfo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Controls Pane"));
	NBackControlsInfo->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackControlsInfo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackControlsInfo->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "ControlsInfoLocation"));
	FString PathToMeshNBackControls = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_ControlsPane.SM_ControlsPane'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackControlsMeshObj(*PathToMeshNBackControls);
	NBackControlsInfo->SetStaticMesh(NBackControlsMeshObj.Object);
	NBackControlsInfo->SetCastShadow(false);

	// Creating a pane for the title (0-back, 1-back, etc..)
	NBackTitle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Title Pane"));
	NBackTitle->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackTitle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackTitle->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "TitleLocation"));
	FString PathToMeshNBackTitle = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_NBackTitle.SM_NBackTitle'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackTitleMeshObj(*PathToMeshNBackTitle);
	NBackTitle->SetStaticMesh(NBackTitleMeshObj.Object);
	NBackTitle->SetCastShadow(false);

	// Setting the appropriate n-back task title
	SetNBackTitle(static_cast<int>(CurrentNValue));

	// Construct the progress bar for the n-back task trial
	ProgressWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("N-back progress bar"));
	ProgressWidgetComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	// Construct all the sounds for Logitech inputs
	static ConstructorHelpers::FObjectFinder<USoundWave> CorrectSoundWave(
		TEXT("SoundWave'/Game/NDRT/NBackTask/Sounds/CorrectNBackSound.CorrectNBackSound'"));
	NBackCorrectSound = CreateDefaultSubobject<UAudioComponent>(TEXT("CorrectNBackSound"));
	NBackCorrectSound->SetupAttachment(GetRootComponent());
	NBackCorrectSound->bAutoActivate = false;
	NBackCorrectSound->SetSound(CorrectSoundWave.Object);

	static ConstructorHelpers::FObjectFinder<USoundWave> IncorrectSoundWave(
		TEXT("SoundWave'/Game/NDRT/NBackTask/Sounds/IncorrectNBackSound.IncorrectNBackSound'"));
	NBackIncorrectSound = CreateDefaultSubobject<UAudioComponent>(TEXT("IncorrectNBackSound"));
	NBackIncorrectSound->SetupAttachment(GetRootComponent());
	NBackIncorrectSound->bAutoActivate = false;
	NBackIncorrectSound->SetSound(IncorrectSoundWave.Object);
}

void AEgoVehicle::ConstructTVShowElements()
{
	// Initializing the static mesh for the media player with a default texture
	MediaPlayerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TV-show Pane"));
	UStaticMesh *CubeMesh = ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'")).Object;
	MediaPlayerMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, TEXT("Material'/Game/NDRT/TVShow/MediaPlayer/M_MediaPlayerDefault.M_MediaPlayerDefault'")));
	MediaPlayerMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	MediaPlayerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MediaPlayerMesh->SetRelativeTransform(VehicleParams.Get<FTransform>("TVShow", "MediaPlayerLocation"));
	MediaPlayerMesh->SetStaticMesh(CubeMesh);
	MediaPlayerMesh->SetMaterial(0, MediaPlayerMaterial);
	MediaPlayerMesh->SetCastShadow(false);

	// Add a Media sounds component to the static mesh player
	MediaPlayer = Cast<UMediaPlayer>(StaticLoadObject(UMediaPlayer::StaticClass(), nullptr, TEXT("MediaPlayer'/Game/NDRT/TVShow/MediaPlayer/MediaPlayer.MediaPlayer'")));

	// Create a MediaSoundComponent
	MediaSoundComponent = NewObject<UMediaSoundComponent>(MediaPlayerMesh);
	MediaSoundComponent->AttachToComponent(MediaPlayerMesh, FAttachmentTransformRules::KeepRelativeTransform);

	// Set the media player to the sound component
	MediaSoundComponent->SetMediaPlayer(MediaPlayer);

	// Add the MediaSoundComponent to the actor's components
	MediaPlayerMesh->GetOwner()->AddOwnedComponent(MediaSoundComponent);
}

// N-back task exclusive methods

void AEgoVehicle::SetLetter(const FString &Letter)
{
	if (NBackLetter == nullptr)
		return; // NBackLetter is not initialized yet

	FString MaterialPath = FString::Printf(TEXT("Material'/Game/NDRT/NBackTask/Letters/M_%s.M_%s'"), *Letter, *Letter);
	UMaterial *NewMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *MaterialPath));

	if (NewMaterial)
	{
		NBackLetter->SetMaterial(0, NewMaterial);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load material: %s"), *MaterialPath);
	}
}

void AEgoVehicle::RecordNBackInputs(bool BtnUp, bool BtnDown)
{
	// Check if there is any conflict of inputs. If yes, then ignore and wait for another input
	if (BtnUp && BtnDown)
	{
		return;
	}

	// Ignore input if they are not intended for NDRT (for e.g the driver presses a button by mistake when taking over)
	if (CurrVehicleStatus == VehicleStatus::TakeOver || CurrVehicleStatus == VehicleStatus::TakeOverManual)
	{
		return;
	}

	// Now, record the input if all the above conditions are satisfied
	if (BtnUp && !bWasBtnUpPressedLastFrame)
	{
		NBackResponseBuffer.Add(TEXT("M"));
	}
	else if (BtnDown && !bWasBtnDownPressedLastFrame)
	{
		NBackResponseBuffer.Add(TEXT("MM"));
	}

	// Update the previous state for the next frame
	bWasBtnUpPressedLastFrame = BtnUp;
	bWasBtnDownPressedLastFrame = BtnDown;
}

void AEgoVehicle::NBackTaskTick()
{
	// Note: This method, we assume that NBackPrompts is already initialized with randomized letters
	// There are two cases when computation is required.
	// CASE 1: [Response already registered] Input is given and time has not expired
	// CASE 2: [Response already registered] Time has expired (go to the next trial)
	// CASE 3: [Response not registered] Input is given and time has not expired
	// CASE 4: [Response not registered] Time has expired (go to the next trial)
	const float TrialTimeLimit = OneBackTimeLimit + 1.0 * (static_cast<int>(CurrentNValue) - 1);
	const bool HasTimeExpired = FPlatformTime::Seconds() - NBackTrialStartTimestamp >= TrialTimeLimit;
	UpdateProgressBar(HasTimeExpired ? 0.f : (FPlatformTime::Seconds() - NBackTrialStartTimestamp) / TrialTimeLimit);

	if (IsNBackResponseGiven)
	{
		if (HasTimeExpired)
		{
			// Go to the next trial regardless if whether an input was given or not
			// Check all the n-back task trials are over. If TOR is not finished, add more tasks.
			if (NBackPrompts.Num() == NBackRecordedResponses.Num())
			{
				if (CurrVehicleStatus == VehicleStatus::ResumedAutopilot)
				{
					// We can safely end the trial here
					TerminateNDRT();

					// Exit to not cause index out of bound error
					return;
				}
				else
				{
					// This should ideally never kick in
					int32 AdditionalTasks = static_cast<int32>((OneBackTimeLimit * 10) / (OneBackTimeLimit + 1.0 * (static_cast<int>(CurrentNValue) - 1)));
					for (int32 i = 0; i < 10; i++)
					{
						// NOTE: A "MATCH" is generated 50% times and "MISMATCH" other times
						FString SingleLetter;
						if (FMath::RandBool() && i >= static_cast<int>(CurrentNValue))
						{
							SingleLetter = NBackPrompts[i - static_cast<int>(CurrentNValue)];
						}
						else
						{
							TCHAR RandomChar = FMath::RandRange('A', 'Z');
							SingleLetter = FString(1, &RandomChar);
						}
						NBackPrompts.Add(SingleLetter);
					}
				}
			}
			// Set the next letter if there are more prompts left
			SetLetter(NBackPrompts[NBackRecordedResponses.Num()]);

			// Since a new letter is set, update the time stamp
			NBackTrialStartTimestamp = FPlatformTime::Seconds();

			// Reset the boolean variable for the new trial now
			IsNBackResponseGiven = false;
		}
		else if (NBackResponseBuffer.Num() > 0)
		{
			// Clear the response buffer as the input is already registered.
			NBackResponseBuffer.Empty();
		}
	}
	else
	{
		FString LatestResponse;
		if (NBackResponseBuffer.Num() > 0)
		{
			LatestResponse = NBackResponseBuffer[0];
		}
		else if (HasTimeExpired)
		{
			LatestResponse = "NR"; // No Response
		}
		else
		{
			return;
		}

		IsNBackResponseGiven = true;

		// Get the current game index
		int32 CurrentGameIndex = NBackRecordedResponses.Num();

		// Just a safety check so that the simulator does not crash because of index out of bounds
		// NOTE: This will only be true when NDRT is terminated, but NBackTaskTick() is still called
		// due to a small lag in ending the trial
		if (NBackPrompts.Num() == NBackRecordedResponses.Num())
		{
			return;
		}
		// Figure out if there is a match or not
		FString CorrectResponse;
		if (CurrentGameIndex < static_cast<int>(CurrentNValue))
		{
			CorrectResponse = TEXT("MM");
		}
		else
		{
			if (NBackPrompts[CurrentGameIndex].Equals(NBackPrompts[CurrentGameIndex - static_cast<int>(CurrentNValue)]))
			{
				CorrectResponse = TEXT("M");
			}
			else
			{
				CorrectResponse = TEXT("MM");
			}
		}

		// Check if the expected response matches the given response
		if (CorrectResponse.Equals(LatestResponse))
		{
			// Play a "correct answer" sound
			NBackCorrectSound->Play();
		}
		else
		{
			// Play an "incorrect answer" sound
			NBackIncorrectSound->Play();
		}

		// Now, add the latest response to the array just for record
		NBackRecordedResponses.Add(LatestResponse);

		// Get the current timestamp and record it
		FDateTime CurrentTime = FDateTime::Now();
		FString TimestampWithoutMilliseconds = CurrentTime.ToString(TEXT("%d/%m/%Y %H:%M:%S"));
		int32 Milliseconds = CurrentTime.GetMillisecond();
		FString Timestamp = FString::Printf(TEXT("%s.%03d"), *TimestampWithoutMilliseconds, Milliseconds);
		NBackResponseTimestamp.Add(Timestamp);

		// We can now clear the response buffer
		NBackResponseBuffer.Empty();
	}
}

void AEgoVehicle::SetNBackTitle(int32 NBackValue)
{
	// Changing the title dynamically based on the n-back task.
	// NOTE: We will have to use StaticLoadObject instead of FObjectFinder due to the runtime nature.
	// NOTE: Assumes NBackTitle is initialized

	const FString MaterialPath = FString::Printf(TEXT("Material'/Game/NDRT/NBackTask/Titles/M_%dBackTaskTitle.M_%dBackTaskTitle'"), NBackValue, NBackValue);
	UMaterial *NewMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *MaterialPath));
	NBackTitle->SetMaterial(0, NewMaterial);
}

// TV show task exclusive methods

void AEgoVehicle::TVShowTaskTick()
{
}

// Misc methods
void AEgoVehicle::SetMessagePaneText(FString DisplayText, FColor TextColor)
{
	MessagePane->SetTextRenderColor(TextColor);
	MessagePane->SetText(DisplayText);
}

void AEgoVehicle::SetHUDTimeThreshold(float Threshold)
{
	GazeOnHUDTimeConstraint = Threshold;
}

void AEgoVehicle::UpdateProgressBar(float NewProgressValue)
{
	UUserWidget *UserWidget = ProgressWidgetComponent->GetUserWidgetObject();
	if (UserWidget)
	{
		UNBackProgressBar *ProgressBarWidget = Cast<UNBackProgressBar>(UserWidget);
		if (ProgressBarWidget)
		{
			ProgressBarWidget->SetProgress(NewProgressValue);
		}
	}
}

// Construct HUD debugger

void AEgoVehicle::ConstructHUDDebugger()
{
	// Construct the pane responsible for setting eye-tracker HUD location boolean here
	OnSurfValue = CreateEgoObject<UTextRenderComponent>("OnSurfValue");
	OnSurfValue->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	OnSurfValue->SetRelativeTransform(VehicleParams.Get<FTransform>("HUDDebugger", "OnSurfLocation"));
	OnSurfValue->SetTextRenderColor(FColor::Black);
	OnSurfValue->SetText(FText::FromString(""));
	OnSurfValue->SetWorldSize(5); // scale the font with this
	OnSurfValue->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	OnSurfValue->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);

	// Construct the pane responsible for setting eye-tracker HUD location boolean here
	HUDGazeTime = CreateEgoObject<UTextRenderComponent>("HUDGazeTime");
	HUDGazeTime->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	HUDGazeTime->SetRelativeTransform(VehicleParams.Get<FTransform>("HUDDebugger", "HUDGazeTimerLocation"));
	HUDGazeTime->SetTextRenderColor(FColor::Black);
	HUDGazeTime->SetText(FText::FromString(""));
	HUDGazeTime->SetWorldSize(5); // scale the font with this
	HUDGazeTime->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	HUDGazeTime->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
}

void AEgoVehicle::HUDDebuggerTick()
{
	// Set the OnSurf Value here
	FString BoolAsString = bLatestOnSurfValue ? TEXT("True") : TEXT("False");
	OnSurfValue->SetTextRenderColor(bLatestOnSurfValue ? FColor::Green : FColor::Red);
	FString OnSurf = FString::Printf(TEXT("OnSurf: %s"), *BoolAsString);
	OnSurfValue->SetText(FText::FromString(OnSurf));

	// Set the HUD Timer Value here
	float GazeTime = GazeOnHUDTime();
	HUDGazeTime->SetTextRenderColor(FMath::IsNearlyEqual(GazeTime, 0.f, 0.0001f) ? FColor::Red : FColor::Green);
	FString GazeTimeString = FString::Printf(TEXT("Gaze Time: %.3f"), GazeTime);
	HUDGazeTime->SetText(FText::FromString(GazeTimeString));
}