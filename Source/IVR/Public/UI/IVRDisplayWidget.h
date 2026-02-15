// IVR/Source/IVR/Public/UI/IVRDisplayWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"       // Necessário para UImage* (se usar Binding por C++)
#include "Engine/Texture2D.h"       // Necessário para UTexture2D*
#include "Components/IVRCaptureComponent.h" // Inclua o cabeçalho do seu componente de captura
#include "IVRTypes.h"          // Necessário para FIVR_JustRTFrame

#include "IVRDisplayWidget.generated.h"

/**
 * @brief Widget UMG customizado para exibir a textura em tempo real do IVRCaptureComponent.
 */
UCLASS()
class IVR_API UIVRDisplayWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Esta propriedade UTexture2D* será a ponte para a textura exibida no UMG.
    // Ela será atualizada a cada novo frame recebido do IVRCaptureComponent.
    UPROPERTY(BlueprintReadOnly, Category = "IVR|Display")
    UTexture2D* CurrentLiveTexture;

    // Função para definir o componente de captura ao qual este widget está vinculado.
    // Chamável a partir de Blueprints.
    UFUNCTION(BlueprintCallable, Category = "IVR|Display")
    void SetCaptureComponent(UIVRCaptureComponent* InCaptureComponent);

protected:
    // Chamado quando o widget é construído (equivalente ao BeginPlay para Widgets).
    virtual void NativeConstruct() override;
    // Chamado quando o widget é destruído (equivalente ao EndPlay para Widgets).
    virtual void NativeDestruct() override;

private:
    // Referência privada para o componente de captura, para gerenciar a inscrição/desinscrição.
    UPROPERTY()
    UIVRCaptureComponent* LinkedCaptureComponent;

    // Manipulador para o delegate OnRealTimeFrameReady do IVRCaptureComponent.
    // Este método receberá a estrutura FIVR_JustRTFrame que contém a LiveTexture.
    UFUNCTION()
    void OnRealTimeFrameReadyHandler(const FIVR_JustRTFrame& FrameData);
};
