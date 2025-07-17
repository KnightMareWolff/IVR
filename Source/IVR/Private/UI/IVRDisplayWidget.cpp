// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "UI/IVRDisplayWidget.h"

void UIVRDisplayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Verifica se o DisplayImage foi vinculado corretamente pelo meta = (BindWidget)
    if (!DisplayImage)
    {
        UE_LOG(LogTemp, Warning, TEXT("UIVRDisplayWidget: DisplayImage UImage component is null. "
                                      "Ensure a UImage widget named 'DisplayImage' exists in your UMG Blueprint "
                                      "that inherits from this C++ class, and that 'Is Variable' is checked."));
    }
}

void UIVRDisplayWidget::SetDisplayTexture(UTexture2D* NewTexture)
{
    if (DisplayImage)
    {
        // Define a textura usando um Brush.
        // O UMG gerenciar� o redimensionamento do Image para o tamanho da textura.
        DisplayImage->SetBrushFromTexture(NewTexture);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UIVRDisplayWidget: Cannot set texture, DisplayImage is null."));
    }
}

