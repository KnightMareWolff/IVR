// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"      // Para o mecanismo de "locked rendering"
#include "HAL/PlatformProcess.h"

// Inclua seu LogCategory
#include "IVR.h" 

/**
 * Classe para ler continuamente a sa�da de um processo externo (FFmpeg)
 * e logar no console da Unreal Engine.
 */
class FFMpegLogReader : public FRunnable
{
public:
    // NOVO: Adicionado um par�metro de prefixo para o log
    FFMpegLogReader(void* InReadPipe, const FString& InLogPrefix = TEXT("FFmpeg Output"));
    virtual ~FFMpegLogReader();

    // FRunnable interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;

    // Fun��o para iniciar e parar a thread
    void Start();
    void EnsureCompletion();

private:
    void* ReadPipe; // O handle de leitura do pipe para o stdout/stderr do FFmpeg
    FRunnableThread* Thread;
    FThreadSafeBool bShouldStop; // Flag para parar a thread com seguran�a
    FString LogPrefix; // NOVO: Prefixo para diferenciar as mensagens de log
};


