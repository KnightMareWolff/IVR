// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HAL/PlatformMisc.h"

// [MANUAL_REF_POINT] Includes do OpenCV foram movidos para IVROpenCVBridge.

#include "IVRGlobalStatics.generated.h"

// Definição da USTRUCT para empacotar os detalhes do erro
USTRUCT(BlueprintType)
struct IVR_API FIVR_SystemErrorDetails
{
    GENERATED_BODY()

    // O código numérico do erro (usando int32 para compatibilidade com Blueprint)
    UPROPERTY(BlueprintReadOnly, Category = "Error Details")
    int32 ErrorCode = 0;
    // A descrição textual do erro
    UPROPERTY(BlueprintReadOnly, Category = "Error Details")
    FString ErrorDescription;
};

/**
 * 
 */
UCLASS()
class IVR_API UIVRGlobalStatics : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    /**
    * Retorna a descrição do último erro do sistema operacional.
    * Esta função é multiplataforma, utilizando as abstrações da Unreal Engine.
    * @param ErrorCode Opcional. O código de erro a ser traduzido. Se 0, usa o último erro da thread.
    * @return Uma FString contendo a descrição do erro.
    */
    UFUNCTION(BlueprintCallable, Category = "IVR System|Error Handling",
              meta = (DisplayName = "Get Last System Error Details",
              ToolTip = "Retrieves the last system error code and its description, multi-platform aware.",
              Keywords = "error, system, last, code, description, platform, ivr"))
    static FIVR_SystemErrorDetails GetLastSystemErrorDetails();
};