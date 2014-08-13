// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProgressBar.generated.h"

/** ProgressBar widget */
UCLASS(meta=( Category="Common" ), ClassGroup=UserInterface)
class UMG_API UProgressBar : public UWidget
{
	GENERATED_UCLASS_BODY()
	
public:

	/** Style used for the progress bar */
	UPROPERTY(EditDefaultsOnly, Category="Style", meta=( DisplayThumbnail = "true" ))
	USlateWidgetStyleAsset* Style;

	/** The brush to use as the background of the progress bar */
	UPROPERTY(EditDefaultsOnly, Category="Style", meta=( DisplayThumbnail = "true" ), AdvancedDisplay)
	USlateBrushAsset* BackgroundImage;
	
	/** The brush to use as the fill image */
	UPROPERTY(EditDefaultsOnly, Category="Style", meta=( DisplayThumbnail = "true" ), AdvancedDisplay)
	USlateBrushAsset* FillImage;
	
	/** The brush to use as the marquee image */
	UPROPERTY(EditDefaultsOnly, Category="Style", meta=( DisplayThumbnail = "true" ), AdvancedDisplay)
	USlateBrushAsset* MarqueeImage;

	/** Defines if this progress bar fills Left to right or right to left */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	TEnumAsByte<EProgressBarFillType::Type> BarFillType;
	
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	bool bIsMarquee;

	/** Used to determine the fill position of the progress bar ranging 0..1 */
	UPROPERTY(EditDefaultsOnly, Category=Appearance, meta=( UIMin = "0", UIMax = "1", editcondition = "bOverride_Percent" ))
	float Percent;

	/** A bindable delegate to allow logic to drive the text of the widget */
	UPROPERTY()
	FGetFloat PercentDelegate;

	/** Fill Color and Opacity */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FLinearColor FillColorAndOpacity;

	/** Border Padding around fill bar */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FVector2D BorderPadding;

public:
	
	/** Sets the current value of the ProgressBar. */
	UFUNCTION(BlueprintCallable, Category="Behavior")
	void SetPercent(float InPercent);
	
	// UWidget interface
	virtual void SynchronizeProperties() override;
	// End of UWidget interface

#if WITH_EDITOR
	// UWidget interface
	virtual const FSlateBrush* GetEditorIcon() override;
	// End UWidget interface
#endif

protected:
	/** Native Slate Widget */
	TSharedPtr<SProgressBar> MyProgressBar;

	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget interface
};
