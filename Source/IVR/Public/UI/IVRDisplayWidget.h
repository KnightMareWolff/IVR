// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h" // Necess�rio para UImage
#include "Engine/Texture2D.h" // Necess�rio para UTexture2D
#include "IVRDisplayWidget.generated.h"

/**
 * Widget de exemplo para exibir uma textura 2D.
 * Pode ser herdado em Blueprint para construir a interface visual.
 */
UCLASS()
class IVR_API UIVRDisplayWidget : public UUserWidget
{
    GENERATED_BODY()
    
public:
    // O widget de imagem que ir� exibir a textura.
    // Marque com BindWidget para que o framework UMG o injete automaticamente
    // se houver um UImage com o mesmo nome (DisplayImage) no seu Blueprint Widget.
    UPROPERTY(meta = (BindWidget))
    UImage* DisplayImage;

    /**
     * Define a textura a ser exibida no widget de imagem.
     * @param NewTexture A nova textura a ser usada.
     */
    UFUNCTION(BlueprintCallable, Category = "IVR|Display")
    void SetDisplayTexture(UTexture2D* NewTexture);

protected:
    virtual void NativeConstruct() override;
};

