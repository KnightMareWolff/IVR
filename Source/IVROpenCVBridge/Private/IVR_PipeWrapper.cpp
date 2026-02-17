// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#include "IVR_PipeWrapper.h"
//#include "IVRGlobalStatics.h" // Para o tratamento de erros (este é do módulo IVR, mas está ok chamar)
#include "Misc/Guid.h" // Para gerar GUIDs
#include "Misc/FileHelper.h" // Para FFileHelper::DeleteFile
#include "HAL/PlatformMisc.h" // Para FPlatformMisc::GetLastError
#include "Misc/Paths.h" // Para FPaths::GetTempDir
#include "HAL/PlatformFileManager.h" // Para IPlatformFileManager (adicionado para mkpath)
#include "HAL/FileManager.h" // Para IFileManager (adicionado para mkpath)


// Definição de LogCategory (agora do módulo IVR, se for usado o LogIVR.
// Se quiser um log separado, defina LogIVRPipeWrapper no IVROpenCVBridge.h/.cpp)
DEFINE_LOG_CATEGORY(LogIVRPipeWrapper);
// Constructor
FIVR_PipeWrapper::FIVR_PipeWrapper()
    : FullPipePath(TEXT(""))
    , bIsCreatedAndConnected(false)
{
#if PLATFORM_WINDOWS
    PipeHandle = INVALID_HANDLE_VALUE;
#elif PLATFORM_LINUX || PLATFORM_MAC
    FileDescriptor = -1;
#endif
}

// Destructor
FIVR_PipeWrapper::~FIVR_PipeWrapper()
{
    Close(); // Garante que o pipe é fechado quando o objeto é destruído
}

// Create Pipe
bool FIVR_PipeWrapper::Create(const FIVR_PipeSettings& Settings, const FString& SessionID, int32 InWidth, int32 InHeight)
{
    if (IsValid())
    {
        UE_LOG(LogIVRPipeWrapper, Warning, TEXT("Pipe '%s' já criado. Fechando e recriando."), *FullPipePath);
        Close();
    }

    PipeSettings = Settings;
    FString BasePipeName = Settings.BasePipeName;

    // Construir o caminho completo do pipe.
    // Para Windows: \.\pipe\<BasePipeName>_<SessionID>
    // Para POSIX: /tmp/<BasePipeName>_<SessionID> (ou outro diretório temporário seguro)
    FString UniquePipeName = FString::Printf(TEXT("%s%s"), *BasePipeName , *SessionID);

#if PLATFORM_WINDOWS
    FullPipePath = FString::Printf(TEXT("\\\\.\\pipe\\%s"), *UniquePipeName);
    DWORD OpenMode = PIPE_ACCESS_OUTBOUND; // UE escreve, FFmpeg lê
    DWORD PipeMode = PIPE_TYPE_BYTE; // Modo byte stream
    if (Settings.bBlockingMode)
    {
        PipeMode |= PIPE_WAIT; // Modo bloqueante
    }
    else
    {
        PipeMode |= PIPE_NOWAIT; // Modo não-bloqueante
    }
    DWORD CalculatedBufferSize = Settings.OutBufferSize; // Valor padrão da PipeSettings
    if (InWidth > 0 && InHeight > 0)
    {
        // Assumindo BGRA (4 bytes por pixel)
        CalculatedBufferSize = InWidth * InHeight * 4;
        // O Windows pode ter um limite máximo para o buffer de Named Pipes.
        // Um valor muito grande pode ser internamente truncado.
        // 8MB (para 1080p BGRA) é geralmente OK, mas é bom ter em mente.
    }
    // Cria a instância do Named Pipe
    PipeHandle = CreateNamedPipe(
        *FullPipePath,            // Nome do pipe
        OpenMode,                 // Modo de abertura
        PipeMode,                 // Modo do pipe
        1,                        // Número máximo de instâncias
        CalculatedBufferSize,     // Tamanho do buffer de saída
        CalculatedBufferSize,     // Tamanho do buffer de entrada
        NMPWAIT_USE_DEFAULT_WAIT, // Timeout padrão para cliente conectar
        NULL);                    // Atributos de segurança (padrão)
// FIVR_SystemErrorDetails Det = UIVRGlobalStatics::GetLastSystemErrorDetails(); // UIVRGlobalStatics é UObject, não pode ser incluído aqui.
    // Apenas loga o erro bruto.
    if (PipeHandle == INVALID_HANDLE_VALUE) // Corrigido para verificar INVALID_HANDLE_VALUE, não um ponteiro nulo.
    {
        DWORD ErrorCode = GetLastError();
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to create Windows Named Pipe '%s'. Error: %d"), *FullPipePath, ErrorCode);
        return false;
    }

    // Apenas defina bIsCreatedAndConnected como true, pois o pipe foi criado com sucesso.
    bIsCreatedAndConnected = true;
    UE_LOG(LogIVRPipeWrapper, Log, TEXT("Windows Named Pipe '%s' created successfully and awaiting client connection."), *FullPipePath);
#elif PLATFORM_LINUX || PLATFORM_MAC
    // Para POSIX, usar um diretório temporário seguro dentro do projeto.
    // FPaths::ProjectSavedDir() é um local garantido e adequado para arquivos temporários do plugin.
    // Criamos um subdiretório para organizar os pipes temporários.
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FString TempPipeDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("IVRTemporaryPipes"));
    if (!PlatformFile.DirectoryExists(*TempPipeDir))
    {
        PlatformFile.CreateDirectoryTree(*TempPipeDir);
        UE_LOG(LogIVRPipeWrapper, Log, TEXT("Created temporary pipe directory: %s"), *TempPipeDir);
    }

    FullPipePath = FPaths::Combine(TempPipeDir, UniquePipeName);
    // Tenta deletar o FIFO se ele já existir, para garantir um novo começo limpo.
    // mkfifo falharia se o arquivo já existisse.
    if (access(TCHAR_TO_UTF8(*FullPipePath), F_OK) == 0)
    {
        if (unlink(TCHAR_TO_UTF8(*FullPipePath)) == 0)
        {
            UE_LOG(LogIVRPipeWrapper, Warning, TEXT("Removed existing FIFO file '%s' for clean start."), *FullPipePath);
        }
        else
        {
            UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to remove existing FIFO file '%s'. Error: %s"), *FullPipePath, UTF8_TO_TCHAR(strerror(errno)));
            // Não é um erro fatal para a criação, mas é um aviso.
        }
    }
    // Criar o FIFO (mkfifo)
    // Permissões 0666 para read/write para owner, group, others.
    if (mkfifo(TCHAR_TO_UTF8(*FullPipePath), 0666) == -1)
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to create FIFO '%s'. Error: %s"), *FullPipePath, UTF8_TO_TCHAR(strerror(errno)));
        return false;
    }
    
    // Apenas criamos o FIFO no sistema de arquivos. A abertura para escrita (que bloqueia)
    // será feita em Connect().
    bIsCreatedAndConnected = true;
    UE_LOG(LogIVRPipeWrapper, Log, TEXT("FIFO '%s' created successfully. Opening for writing will happen in Connect()."), *FullPipePath);

#else // Outras plataformas (se houver)
    UE_LOG(LogIVRPipeWrapper, Error, TEXT("FIVR_PipeWrapper::Create not implemented for this platform."));
    return false;
#endif

    return bIsCreatedAndConnected;

}

// NOVO MÉTODO DE CONEXÃO
bool FIVR_PipeWrapper::Connect()
{
    if (!bIsCreatedAndConnected) // Verifica se o FIFO foi criado por Create()
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Cannot connect. Pipe was not created. Call Create() first."));
        return false;
    }

#if PLATFORM_WINDOWS
    // ConnectNamedPipe é a chamada bloqueante no Windows
    if (PipeHandle == INVALID_HANDLE_VALUE) // Caso o handle tenha se invalidado.
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Cannot connect. Invalid PipeHandle for Windows Named Pipe '%s'."), *FullPipePath);
        return false;
    }
    
    UE_LOG(LogIVRPipeWrapper, Log, TEXT("Awaiting client connection for pipe: %s"), *FullPipePath);
    BOOL bSuccess = ConnectNamedPipe(PipeHandle, NULL);
    if (!bSuccess && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        DWORD ErrorCode = GetLastError();
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to connect Named Pipe '%s'. Error: %d"), *FullPipePath, ErrorCode);
        CloseHandle(PipeHandle);
        PipeHandle = INVALID_HANDLE_VALUE; // Invalida o handle em caso de erro de conexão.
        return false;
    }
    UE_LOG(LogIVRPipeWrapper, Log, TEXT("Named Pipe '%s' connected successfully."), *FullPipePath);
    return true;
#elif PLATFORM_LINUX || PLATFORM_MAC
    // Para POSIX, mover a chamada open() bloqueante para Connect()
    if (FileDescriptor != -1) // Se já estiver aberto (ex: tentativa anterior de Connect()), consideramos conectado.
    {
        UE_LOG(LogIVRPipeWrapper, Log, TEXT("FIFO '%s' already open for writing (FileDescriptor: %d)."), *FullPipePath, FileDescriptor);
        return true;
    }
    
    // Abrir o FIFO para escrita. Esta é a chamada que BLOQUEIA se não houver leitor.
    UE_LOG(LogIVRPipeWrapper, Log, TEXT("Opening FIFO '%s' for writing... (will block if no reader)"), *FullPipePath);
    // Não usar O_NONBLOCK aqui, para que 'open()' bloqueie até que um leitor se conecte.
    FileDescriptor = open(TCHAR_TO_UTF8(*FullPipePath), O_WRONLY);

    if (FileDescriptor == -1)
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to open FIFO '%s' for writing. Error: %s"), *FullPipePath, UTF8_TO_TCHAR(strerror(errno)));
        // Não precisa de unlink aqui, o FIFO já existe, apenas falhou a abertura para escrita.
        return false;
    }
    UE_LOG(LogIVRPipeWrapper, Log, TEXT("FIFO '%s' opened successfully for writing (FileDescriptor: %d)."), *FullPipePath, FileDescriptor);
    return true;
#else
    UE_LOG(LogIVRPipeWrapper, Warning, TEXT("Connect() not explicitly implemented for this platform. Defaulting to true."));
    return true;
#endif
}
// Write to Pipe
int32 FIVR_PipeWrapper::Write(const uint8* Data, int32 NumBytes)
{
    if (!IsValid())
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Attempted to write to an invalid or uninitialized pipe."));
        return -1;
    }

#if PLATFORM_WINDOWS
    DWORD BytesWritten;
    if (!WriteFile(PipeHandle, Data, NumBytes, &BytesWritten, nullptr))
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to write to Windows Named Pipe '%s'. Error: %d"), *FullPipePath, GetLastError());
        return -1;
    }
    if (BytesWritten != (DWORD)NumBytes)
    {
        UE_LOG(LogIVRPipeWrapper, Warning, TEXT("Incomplete write to Named Pipe '%s'. Wrote %d bytes out of %d requested."), *FullPipePath, BytesWritten, NumBytes);
    }

    return (int32)BytesWritten;

#elif PLATFORM_LINUX || PLATFORM_MAC
    ssize_t BytesWritten = write(FileDescriptor, Data, NumBytes);
    if (BytesWritten == -1)
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to write to FIFO '%s'. Error: %s"), *FullPipePath, UTF8_TO_TCHAR(strerror(errno)));
        return -1;
    }
    return (int32)BytesWritten;

#else // Outras plataformas
    UE_LOG(LogIVRPipeWrapper, Error, TEXT("FIVR_PipeWrapper::Write not implemented for this platform."));
    return -1;
#endif

}
// Close Pipe
void FIVR_PipeWrapper::Close()
{
    if (!bIsCreatedAndConnected) // Se nem sequer foi criado, não há o que fechar.
    {
        return;
    }

#if PLATFORM_WINDOWS
    if (PipeHandle != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(PipeHandle); // Garante que todos os dados sejam gravados
        DisconnectNamedPipe(PipeHandle); // Desconecta o cliente
        CloseHandle(PipeHandle);         // Fecha o handle
        PipeHandle = INVALID_HANDLE_VALUE;
        UE_LOG(LogIVRPipeWrapper, Log, TEXT("Windows Named Pipe '%s' closed."), *FullPipePath);
    }
#elif PLATFORM_LINUX || PLATFORM_MAC
    if (FileDescriptor != -1)
    {
        close(FileDescriptor); // Fecha o descritor de arquivo
        FileDescriptor = -1;
        UE_LOG(LogIVRPipeWrapper, Log, TEXT("FIFO '%s' file descriptor closed."), *FullPipePath);
    }
    // No Linux, o arquivo FIFO em si é limpo quando o módulo IVROpenCVBridge é desligado
    // (ex: na ShutdownModule() ou um Clean() mais abrangente).
    // Evitamos unlink() aqui para não remover o FIFO enquanto o FFmpeg ainda pode estar lendo dele,
    // ou se o processo precisar reabrir o pipe rapidamente.
    // O ideal é que o 'unlink' do FIFO seja feito *antes* de 'mkfifo' para garantir uma limpeza antes da criação,
    // ou no momento de desligamento do módulo/plugin, se o FIFO for persistir durante a execução do processo.
    // Para o nosso caso, a criação já faz um 'unlink' para limpar.
    // if (!FullPipePath.IsEmpty() && access(TCHAR_TO_UTF8(*FullPipePath), F_OK) == 0)
    // {
    //     unlink(TCHAR_TO_UTF8(*FullPipePath));
    //     UE_LOG(LogIVRPipeWrapper, Log, TEXT("FIFO file '%s' unlinked."), *FullPipePath);
    // }
#else // Outras plataformas
    // No-op
#endif
    bIsCreatedAndConnected = false;
    FullPipePath = TEXT(""); // Limpa o caminho para indicar que não há pipe ativo.
}
bool FIVR_PipeWrapper::IsValid() const
{
#if PLATFORM_WINDOWS
    return PipeHandle != INVALID_HANDLE_VALUE && bIsCreatedAndConnected;
#elif PLATFORM_LINUX || PLATFORM_MAC
    // No FIFO (Linux/Mac), precisamos que o FIFO tenha sido criado (bIsCreatedAndConnected)
    // E que a operação de 'open()' para escrita tenha sido bem-sucedida (FileDescriptor != -1)
    return bIsCreatedAndConnected && FileDescriptor != -1;
#else // Outras plataformas
    return false;
#endif
}
FString FIVR_PipeWrapper::GetFullPipeName() const
{
    return FullPipePath;
}
