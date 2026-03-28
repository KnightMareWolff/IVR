// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "Recording/IVRVideoEncoder.h"
#include "IVR.h"
#include "Misc/Guid.h"         // Para FGuid::NewGuid()
#include "HAL/PlatformProcess.h" // Para FPlatformProcess
#include "HAL/PlatformTime.h"    // Para FPlatformTime::Seconds()
#include "HAL/PlatformFileManager.h" // Para IPlatformFileManager
#include "Misc/FileHelper.h"     // Para FFileHelper
#include "Internationalization/Text.h" // Para FText e FText::Format
#include "Async/Async.h" // Para UE_LOG no thread
#include "Misc/Paths.h" // Para FPaths
// [MANUAL_REF_POINT] FFMpegLogReader e FIVR_PipeWrapper são agora de IVROpenCVBridge
#include "IVROpenCVBridge/Public/FFmpegLogReader.h"
#include "IVROpenCVBridge/Public/IVR_PipeWrapper.h"
// [MANUAL_REF_POINT] FVideoEncoderWorker agora é de IVROpenCVBridge
#include "IVROpenCVBridge/Public/FVideoEncoderWorker.h"
// Definição do LogCategory
DEFINE_LOG_CATEGORY(LogIVRVideoEncoder);
// =====================================================================================
// UIVRVideoEncoder Implementation
// =====================================================================================
UIVRVideoEncoder::UIVRVideoEncoder()
    : EncoderCommandFactory(nullptr) 
    , FFmpegProcHandle() 
    , FFmpegStdoutLogReader(nullptr)
    , FFmpegStderrLogReader(nullptr)
    , FFmpegReadPipeStdout(nullptr)
    , FFmpegWritePipeStdout(nullptr)
    , FFmpegReadPipeStderr(nullptr)
    , FFmpegWritePipeStderr(nullptr)
    , bStopWorkerThread(false)
    , bNoMoreFramesToEncode(false)
    , bIsInitialized(false) 
    , WorkerRunnable(nullptr)
    , WorkerThread(nullptr)
    , FramePool(nullptr) // Inicializa o FramePool como nullptr
{
    NewFrameEvent = FPlatformProcess::GetSynchEventFromPool(false);
}
UIVRVideoEncoder::~UIVRVideoEncoder()
{
    // Destructor (BeginDestroy é o principal ponto de limpeza para UObjects)
}
void UIVRVideoEncoder::BeginDestroy()
{
    // Garante que o ShutdownEncoder seja chamado para limpar recursos, caso o UObject seja destruído antes.
    ShutdownEncoder(); 
    
    if (NewFrameEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(NewFrameEvent);
        NewFrameEvent = nullptr;
    }
    Super::BeginDestroy();
}
FString UIVRVideoEncoder::GetFFmpegExecutablePathInternal() const
{
    // Se FFmpegExecutablePath estiver definido no Blueprint (EditAnywhere), use-o.
    if (!FFmpegExecutablePath.IsEmpty())
    {
        return FFmpegExecutablePath;
    }
    // Caso contrário, construa o caminho padrão relativo ao plugin.
    FString PluginDir = FPaths::ProjectPluginsDir() / TEXT("IVR");
    FString Path = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("FFmpeg"), TEXT("Binaries"));
#if PLATFORM_WINDOWS
    Path = FPaths::Combine(Path, TEXT("Win64"), TEXT("ffmpeg.exe"));
#elif PLATFORM_LINUX
    Path = FPaths::Combine(Path, TEXT("Linux"), TEXT("ffmpeg")); 
#elif PLATFORM_MAC
    Path = FPaths::Combine(Path, TEXT("Mac"), TEXT("ffmpeg")); 
#else
    UE_LOG(LogIVRVideoEncoder, Error, TEXT("FFmpeg executable path not defined for current platform!"));
    return FString();
#endif
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool UIVRVideoEncoder::Initialize(const FIVR_VideoSettings& Settings, const FString& InFFmpegExecutablePath, int32 InActualFrameWidth, int32 InActualFrameHeight, UIVRFramePool* InFramePool) 
{
    if (bIsInitialized) 
    {
        UE_LOG(LogIVRVideoEncoder, Warning, TEXT("UIVRVideoEncoder is already initialized. Call ShutdownEncoder() first."));
        return false;
    }
    CurrentSettings = Settings;
    FFmpegExecutablePath = InFFmpegExecutablePath;
    ActualProcessingWidth = InActualFrameWidth;   // Armazena a largura real
    ActualProcessingHeight = InActualFrameHeight; // Armazena a altura real
    FramePool = InFramePool;
    if (!FramePool) // Verifica se o FramePool é válido
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("UIVRVideoEncoder::Initialize: FramePool is null. Cannot initialize."));
        return false;
    }
    // Crie o UIVRECFactory se ainda não existir
    if (!EncoderCommandFactory)
    {
        // Garante que a factory seja gerenciada pelo GC
        EncoderCommandFactory = NewObject<UIVRECFactory>(this, UIVRECFactory::StaticClass(), TEXT("FFmpegCommandFactory")); 
        if (!EncoderCommandFactory)
        {
            UE_LOG(LogIVRVideoEncoder, Error, TEXT("Failed to create UIVRECFactory instance."));
            return false;
        }
    }
    EncoderCommandFactory->IVR_SetVideoSettings(CurrentSettings);
    EncoderCommandFactory->IVR_SetActualVideoDimensions(ActualProcessingWidth, ActualProcessingHeight); // NOVO
    EncoderCommandFactory->IVR_SetExecutablePath(FFmpegExecutablePath);
    EncoderCommandFactory->IVR_SetPipeSettings(); // Define as configurações padrão para o pipe.
    // 1. Gerar um nome de pipe único para esta sessão
    VideoPipeBaseName = FString::Printf(TEXT("IVIPipe%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Mid(0,5));
// 2. Configurar o Named Pipe para vídeo
    FIVR_PipeSettings PipeSettings = EncoderCommandFactory->IVR_GetPipeSettings();
    PipeSettings.BasePipeName = VideoPipeBaseName; // Use o nome único gerado
    PipeSettings.bBlockingMode = true; // Escrita bloqueante para garantir dados sequenciais
    PipeSettings.bMessageMode = false; // Modo byte stream para dados de vídeo raw
    PipeSettings.bDuplexAccess = false; // Apenas o UE escreve, FFmpeg lê
    // Tentar criar o Named Pipe
    // <--- ALTERAÇÃO: Passar a largura e altura reais para o FIVR_PipeWrapper::Create()
    if (!VideoInputPipe.Create(PipeSettings, TEXT(""), ActualProcessingWidth, ActualProcessingHeight)) 
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("Failed to create video Named Pipe: %s"), *VideoInputPipe.GetFullPipeName());
        return false;
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Video Named Pipe created: %s"), *VideoInputPipe.GetFullPipeName());
    // Inicia a worker thread para escrever frames no pipe
    WorkerRunnable = new FVideoEncoderWorker(this, FrameQueue, VideoInputPipe, bStopWorkerThread, bNoMoreFramesToEncode, NewFrameEvent, FramePool);
    WorkerThread = FRunnableThread::Create(WorkerRunnable, TEXT("IVRVideoEncoderWorkerThread"), 0, TPri_Normal);
    if (!WorkerThread)
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("Failed to create video encoder worker thread. Cleaning up."));
        InternalCleanupEncoderResources(); // Cleanup the pipe
        delete WorkerRunnable; 
        WorkerRunnable = nullptr;
        return false;
    }
    bIsInitialized.AtomicSet(true); 
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("UIVRVideoEncoder initialized successfully."));
    return true;
}
bool UIVRVideoEncoder::LaunchEncoder(const FString& LiveOutputFilePath)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("Encoder is not initialized. Call Initialize() first."));
        return false;
    }
    if (FFmpegProcHandle.IsValid())
    {
        UE_LOG(LogIVRVideoEncoder, Warning, TEXT("FFmpeg process is already running. Please call ShutdownEncoder() first."));
        return false;
    }
    // Limpa handle de processo anterior, se houver
    if (FFmpegProcHandle.IsValid())
    {
        UE_LOG(LogIVRVideoEncoder, Warning, TEXT("FFmpeg process already running. Terminating previous process."));
        FPlatformProcess::TerminateProc(FFmpegProcHandle);
        FPlatformProcess::CloseProc(FFmpegProcHandle);
        FFmpegProcHandle.Reset();
    }
    // Setta o caminho de saída do vídeo ao vivo
    EncoderCommandFactory->IVR_SetOutputFilePath(LiveOutputFilePath);
    // Setta o caminho do pipe de entrada para o FFmpeg
    EncoderCommandFactory->IVR_SetInPipePath(VideoInputPipe.GetFullPipeName());
    // Pega Executavel FFmpeg
    FString ExecPath = GetFFmpegExecutablePathInternal();
    if (ExecPath.IsEmpty())
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("FFmpeg executable path is empty. Cannot launch encoder."));
        return false;
    }
    // Constrói o Comando FFmpeg para a gravação ao vivo (ex: libx264)
    EncoderCommandFactory->IVR_BuildLibx264Command();
    FString Arguments = EncoderCommandFactory->IVR_GetEncoderCommand("libx264");
    
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Launching FFmpeg. Executable: %s , Arguments: %s"), *ExecPath, *Arguments);
    // Pipes de Saída e Erro
    FPlatformProcess::CreatePipe(FFmpegReadPipeStdout, FFmpegWritePipeStdout);
    FPlatformProcess::CreatePipe(FFmpegReadPipeStderr, FFmpegWritePipeStderr); 
    
    uint32 LaunchedProcessId = 0;
    FFmpegProcHandle = FPlatformProcess::CreateProc(
        *ExecPath,
        *Arguments,
        false,   // bLaunchDetached
        true,    // bLaunchHidden
        true,    // bLaunchReallyHidden
        &LaunchedProcessId,
        -1,      // PriorityModifier
        nullptr, // OptionalWorkingDirectory
        FFmpegWritePipeStdout, // stdout do FFmpeg vai para este pipe
        FFmpegWritePipeStderr  // stderr do FFmpeg vai para este pipe
    );
    if (!FFmpegProcHandle.IsValid())
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("Failed to create FFmpeg process. Check path and arguments."));
        // Fecha ambos os pares de pipes em caso de falha no lançamento
        FPlatformProcess::ClosePipe(FFmpegReadPipeStdout, FFmpegWritePipeStdout);
        FPlatformProcess::ClosePipe(FFmpegReadPipeStderr, FFmpegWritePipeStderr);
        FFmpegReadPipeStdout = nullptr; FFmpegWritePipeStdout = nullptr;
        FFmpegReadPipeStderr = nullptr; FFmpegWritePipeStderr = nullptr;
        return false;
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("FFmpeg main process launched successfully. PID: %d"), LaunchedProcessId);
    
    // Cria dois FFMpegLogReader, um para stdout e outro para stderr
    FFmpegStdoutLogReader = new FFMpegLogReader(FFmpegReadPipeStdout, TEXT("FFmpeg STDOUT"));
    FFmpegStdoutLogReader->Start();
    FFmpegStderrLogReader = new FFMpegLogReader(FFmpegReadPipeStderr, TEXT("FFmpeg STDERR"));
    FFmpegStderrLogReader->Start();
    // Importante: Feche as extremidades de escrita dos pipes no processo pai, pois o FFmpeg as herdou.
    FPlatformProcess::ClosePipe(nullptr, FFmpegWritePipeStdout);
    FPlatformProcess::ClosePipe(nullptr, FFmpegWritePipeStderr);
    return true;
}
void UIVRVideoEncoder::ShutdownEncoder()
{
    if (!bIsInitialized && !FFmpegProcHandle.IsValid() && !WorkerThread) 
    {
        return;
    }

    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Shutting down UIVRVideoEncoder..."));
// 1. Sinaliza à worker thread para parar
    bStopWorkerThread.AtomicSet(true); 
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda a thread caso esteja esperando por um evento
    // 2. Aguarda a conclusão da worker thread
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
        delete WorkerRunnable;
        WorkerRunnable = nullptr;
    }
    // 3. Limpa os recursos internos (pipes de entrada) e processo FFmpeg
    InternalCleanupEncoderResources();
    bIsInitialized.AtomicSet(false); 
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("UIVRVideoEncoder shut down successfully."));
}
bool UIVRVideoEncoder::EncodeFrame(FIVR_VideoFrame Frame)
{
    if (!bIsInitialized) 
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("UIVRVideoEncoder is not initialized. Cannot encode frame."));
        return false;
    }
    
    if (bNoMoreFramesToEncode) 
    {
        UE_LOG(LogIVRVideoEncoder, Warning, TEXT("UIVRVideoEncoder has been signaled that no more frames are coming. Frame dropped."));
        
        // Libera o frame para o pool se não puder ser enfileirado
        if (FramePool && Frame.RawDataPtr.IsValid())
        {
            FramePool->ReleaseFrame(Frame.RawDataPtr);
        }
        return false;
    }
    // LOG DE DEBUG: Confirma o tamanho do frame antes de enfileirar no Encoder
    // UE_LOG(LogIVRVideoEncoder, Warning, TEXT("UIVRVideoEncoder: Enqueuing frame for worker. RawDataPtr size: %d"), 
    //    Frame.RawDataPtr.IsValid() ? Frame.RawDataPtr->Num() : 0); // Descomente para debug intenso
    FrameQueue.Enqueue(MoveTemp(Frame)); // Usa MoveTemp para otimizar o TSharedPtr
    
    if (NewFrameEvent) NewFrameEvent->Trigger();

    return true;
}

bool UIVRVideoEncoder::FinishEncoding()
{
    if (!bIsInitialized) 
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("UIVRVideoEncoder is not initialized. Cannot finish encoding."));
        return false;
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Signaling UIVRVideoEncoder to finish encoding..."));
    // Sinaliza que não haverá mais frames para codificar
    bNoMoreFramesToEncode.AtomicSet(true);
    if (NewFrameEvent) NewFrameEvent->Trigger(); // Acorda a thread para processar quaisquer frames remanescentes na fila
    // Aguarda ativamente a fila esvaziar para garantir que todos os frames sejam escritos no pipe
    while (!FrameQueue.IsEmpty() && !bStopWorkerThread) 
    {
        FPlatformProcess::Sleep(0.01f); // Pequena pausa para permitir que o worker processe
    }
    // Fecha o pipe de entrada para sinalizar EOF ao FFmpeg.
    // É crucial fechar o pipe APENAS depois que todos os dados foram escritos.
    if (VideoInputPipe.IsValid())
    {
        VideoInputPipe.Close(); 
        UE_LOG(LogIVRVideoEncoder, Log, TEXT("Video input pipe closed, signaling EOF to FFmpeg."));
    }
    
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("UIVRVideoEncoder finished sending video data."));
    return true;
}
bool UIVRVideoEncoder::ConcatenateVideos(const TArray<FString>& InTakePaths, const FString& InMasterOutputPath)
{
    if (InTakePaths.Num() == 0)
    {
        UE_LOG(LogIVRVideoEncoder, Warning, TEXT("No take paths provided for concatenation."));
        return false;
    }
    // 1. Criar um arquivo temporário com a lista de takes
    FString FileListPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("IVRTemp"), TEXT("filelist.txt"));
    FString FileListContent;
    for (const FString& TakePath : InTakePaths)
    {
        FileListContent += FString::Printf(TEXT("file '%s'\n"), *TakePath);
    }
    if (!FFileHelper::SaveStringToFile(FileListContent, *FileListPath))
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("Failed to create filelist.txt for concatenation at: %s"), *FileListPath);
        return false;
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Created filelist for concatenation at: %s"), *FileListPath);
    // 2. Construir o comando FFmpeg para concatenação
    EncoderCommandFactory->IVR_BuildConcatenationCommand(FileListPath, InMasterOutputPath);
    FString ConcatenationArguments = EncoderCommandFactory->IVR_GetEncoderCommand("ConcatenateTakes");
    FString ExecPath = GetFFmpegExecutablePathInternal();
    if (ExecPath.IsEmpty())
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("FFmpeg executable path is empty. Cannot concatenate videos."));
        // Tentar apagar o filelist.txt
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.DeleteFile(*FileListPath);
        return false;
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Launching FFmpeg for concatenation. Executable: %s , Arguments: %s"), *ExecPath, *ConcatenationArguments);
    // 3. Lançar um novo processo FFmpeg para a concatenação
    FProcHandle ConcatenationProcHandle = FPlatformProcess::CreateProc(
        *ExecPath,
        *ConcatenationArguments,
        false,   // bLaunchDetached
        true,    // bLaunchHidden
        true,    // bLaunchReallyHidden
        nullptr, // LaunchedProcessId
        -1,      // PriorityModifier
        nullptr, // OptionalWorkingDirectory
        nullptr, // StdOut (não precisamos capturar para concatenação simples)
        nullptr  // StdErr
    );
    if (!ConcatenationProcHandle.IsValid())
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("Failed to create FFmpeg process for concatenation. Check path and arguments."));
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.DeleteFile(*FileListPath);
        return false;
    }
    // 4. Esperar o processo de concatenação terminar
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Waiting for FFmpeg concatenation process to complete..."));
    FPlatformProcess::WaitForProc(ConcatenationProcHandle);
    
    int32 ReturnCode = -1;
    FPlatformProcess::GetProcReturnCode(ConcatenationProcHandle, &ReturnCode);
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("FFmpeg concatenation process finished with code: %d"), ReturnCode);
    // 5. Limpar recursos
    FPlatformProcess::CloseProc(ConcatenationProcHandle);
    
    // Apagar o arquivo temporário
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.DeleteFile(*FileListPath);
    if (ReturnCode != 0) // FFmpeg retorna 0 para sucesso
    {
        UE_LOG(LogIVRVideoEncoder, Error, TEXT("FFmpeg concatenation failed with return code: %d. Check FFmpeg logs for details."), ReturnCode);
        return false;
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Videos concatenated successfully to: %s"), *InMasterOutputPath);
    return true;
}
// --- INÍCIO DA ALTERAÇÃO: ADICIONAR TIMEOUT AO FECHAMENTO DO PROCESSO FFmpeg ---
void UIVRVideoEncoder::InternalCleanupEncoderResources()
{
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Cleaning up video encoder internal resources..."));
    // Apenas fecha o pipe se ele ainda estiver aberto (FinishEncoding já o faz).
    if (VideoInputPipe.IsValid())
    {
        VideoInputPipe.Close();
        UE_LOG(LogIVRVideoEncoder, Log, TEXT("Video input pipe explicitly closed during cleanup."));
    }
    // Limpa e deleta os leitores de log do FFmpeg (stdout e stderr)
    if (FFmpegStdoutLogReader)
    {
        FFmpegStdoutLogReader->EnsureCompletion();
        delete FFmpegStdoutLogReader;
        FFmpegStdoutLogReader = nullptr;
    }
    if (FFmpegStderrLogReader)
    {
        FFmpegStderrLogReader->EnsureCompletion();
        delete FFmpegStderrLogReader;
        FFmpegStderrLogReader = nullptr;
    }
    
    // Espera o processo FFmpeg terminar completamente, com timeout
    if (FFmpegProcHandle.IsValid())
    {
        UE_LOG(LogIVRVideoEncoder, Log, TEXT("Waiting for main FFmpeg process to complete (with timeout)..."));
        const float MaxWaitTimeSeconds = 5.0f; // Tempo máximo de espera: 5 segundos
        float ElapsedWaitTime = 0.0f;
        while (FPlatformProcess::IsProcRunning(FFmpegProcHandle) && ElapsedWaitTime < MaxWaitTimeSeconds)
        {
            FPlatformProcess::Sleep(0.1f); // Verifica a cada 100ms
            ElapsedWaitTime += 0.1f;
        }
        if (FPlatformProcess::IsProcRunning(FFmpegProcHandle))
        {
            // Se o processo ainda estiver rodando após o timeout, force o encerramento.
            UE_LOG(LogIVRVideoEncoder, Warning, TEXT("FFmpeg process timed out after %.2f seconds. Terminating forcefully."), MaxWaitTimeSeconds);
            FPlatformProcess::TerminateProc(FFmpegProcHandle);
        }
        else
        {
            UE_LOG(LogIVRVideoEncoder, Log, TEXT("Main FFmpeg process finished gracefully."));
        }

        int32 ReturnCode = -1;
        FPlatformProcess::GetProcReturnCode(FFmpegProcHandle, &ReturnCode); 
        UE_LOG(LogIVRVideoEncoder, Log, TEXT("Main FFmpeg process exited with code: %d"), ReturnCode);
        FPlatformProcess::CloseProc(FFmpegProcHandle); // Libera o handle do processo
        FFmpegProcHandle.Reset();
    }
    UE_LOG(LogIVRVideoEncoder, Log, TEXT("Video encoder internal resources cleaned up."));
}
// --- FIM DA ALTERAÇÃO ---