// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HAL/PlatformMisc.h" 

#if WITH_OPENCV
#include "OpenCVHelper.h"
#include "PreOpenCVHeaders.h"

#undef check // the check macro causes problems with opencv headers
#pragma warning(disable: 4668) // 'symbol' not defined as a preprocessor macro, replacing with '0' for 'directives'
#pragma warning(disable: 4828) // The character set in the source file does not support the character used in the literal
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp> // Para cv::VideoCapture
#include <opencv2/imgproc.hpp> // Para cv::cvtColor

#include "PostOpenCVHeaders.h"
#endif

#include "IVRGlobalStatics.generated.h"

// Defini��o da USTRUCT para empacotar os detalhes do erro
USTRUCT(BlueprintType)
struct IVR_API FIVR_SystemErrorDetails
{
    GENERATED_BODY()

    // O c�digo num�rico do erro (usando int32 para compatibilidade com Blueprint)
    UPROPERTY(BlueprintReadOnly, Category = "Error Details")
    int32 ErrorCode = 0; 

    // A descri��o textual do erro
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
	* Retorna a descri��o do �ltimo erro do sistema operacional.
    * Esta fun��o � multiplataforma, utilizando as abstra��es da Unreal Engine.
    * @param ErrorCode Opcional. O c�digo de erro a ser traduzido. Se 0, usa o �ltimo erro da thread.
    * @return Uma FString contendo a descri��o do erro.
    */
    UFUNCTION(BlueprintCallable, Category = "IVR System|Error Handling",
              meta = (DisplayName = "Get Last System Error Details",
              ToolTip = "Retrieves the last system error code and its description, multi-platform aware.",
              Keywords = "error, system, last, code, description, platform, ivr"))
    static FIVR_SystemErrorDetails GetLastSystemErrorDetails();
};


