// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "IVRGlobalStatics.h"
#include "HAL/PlatformMisc.h"  

FIVR_SystemErrorDetails UIVRGlobalStatics::GetLastSystemErrorDetails()
{
    FIVR_SystemErrorDetails ErrorDetails;

    // 1. Captura o �ltimo erro gerado pelo sistema operacional na thread atual
    ErrorDetails.ErrorCode = FPlatformMisc::GetLastError(); // FPlatformMisc::GetLastError() retorna uint32, ser� convertido para int32

    // 2. Obt�m a mensagem de erro do sistema para esse c�digo
    // Define um buffer est�tico (ou alocado dinamicamente se a mensagem puder ser muito grande)
    // para FPlatformMisc::GetSystemErrorMessage escrever a mensagem.
    // UE_ARRAY_COUNT � uma macro �til para obter o tamanho de arrays em TCHARs.
    TCHAR ErrorBuffer[1024]; // Um buffer de 1KB deve ser suficiente para a maioria das mensagens de erro

    // Chama GetSystemErrorMessage para preencher o buffer
    FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, UE_ARRAY_COUNT(ErrorBuffer), ErrorDetails.ErrorCode);

    // Converte o conte�do do buffer para FString
    ErrorDetails.ErrorDescription = ErrorBuffer;

    // Remove quebras de linha/espa�os em branco indesejados do final da string
    ErrorDetails.ErrorDescription.TrimEndInline();

    // 3. Lida com casos onde a mensagem de erro pode estar vazia ou n�o reconhecida
    if (ErrorDetails.ErrorDescription.IsEmpty())
    {
        ErrorDetails.ErrorDescription = FString::Printf(TEXT("Failed to retrieve system error message for code %d. This error code might not be recognized by the system or is generic."), ErrorDetails.ErrorCode);
    }

    return ErrorDetails;
}


