// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "IVR_PipeWrapper.h"
#include "IVRGlobalStatics.h" // Para o tratamento de erros
#include "Misc/Guid.h" // Para gerar GUIDs
#include "Misc/FileHelper.h" // Para FFileHelper::DeleteFile
#include "HAL/PlatformMisc.h" // Para FPlatformMisc::GetLastError
#include "Misc/Paths.h" // Para FPaths::GetTempDir

// Definição de LogCategory
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

    FIVR_SystemErrorDetails Det = UIVRGlobalStatics::GetLastSystemErrorDetails();
    if (Det.ErrorCode != 0)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed CreateNamedPipe %s. Error: %d Description: %s"), *FullPipePath, Det.ErrorCode,*Det.ErrorDescription);
        return false;
    }

    if (!PipeHandle)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to create Windows Named Pipe '%s'. Error: Null Pointer"), *FullPipePath);
        return false;
    }

    // Apenas defina bIsCreatedAndConnected como true, pois o pipe foi criado com sucesso.
    bIsCreatedAndConnected = true;
    UE_LOG(LogIVR, Log, TEXT("Windows Named Pipe '%s' created successfully and awaiting client connection."), *FullPipePath);


#elif PLATFORM_LINUX || PLATFORM_MAC
    // Para POSIX, usar um diretório temporário seguro dentro do projeto.
    // FPaths::ProjectSavedDir() é um local garantido e adequado para arquivos temporários do plugin.
    // Criamos um subdiretório para organizar os pipes temporários.
    FullPipePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("IVRTemporaryPipes"), UniquePipeName);

    // Tenta deletar o FIFO se ele já existir, para garantir um novo começo limpo.
    // mkfifo falharia se o arquivo já existisse.
    if (access(TCHAR_TO_UTF8(*FullPipePath), F_OK) == 0)
    {
        unlink(TCHAR_TO_UTF8(*FullPipePath));
        UE_LOG(LogIVR, Warning, TEXT("Removed existing FIFO file '%s'."), *FullPipePath);
    }

    // Criar o FIFO (mkfifo)
    // Permissões 0666 para read/write para owner, group, others.
    if (mkfifo(TCHAR_TO_UTF8(*FullPipePath), 0666) == -1)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to create FIFO '%s'. Error: %s"), *FullPipePath, UTF8_TO_TCHAR(strerror(errno)));
        return false;
    }
    
    // Abrir o FIFO para escrita. O_WRONLY para escrita, O_NONBLOCK para não bloquear no open.
    // Se bBlockingMode for true, remove O_NONBLOCK para que open() bloqueie até um leitor se conectar.
    int OpenFlags = O_WRONLY;
    if (!Settings.bBlockingMode)
    {
        OpenFlags |= O_NONBLOCK;
    }

    // Abrir o FIFO. Isso pode bloquear se bBlockingMode for true e não houver leitor ainda.
    UE_LOG(LogIVR, Log, TEXT("Opening FIFO '%s' for writing... (will block if no reader)"), *FullPipePath);
    FileDescriptor = open(TCHAR_TO_UTF8(*FullPipePath), OpenFlags);

    if (FileDescriptor == -1)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to open FIFO '%s' for writing. Error: %s"), *FullPipePath, UTF8_TO_TCHAR(strerror(errno)));
        unlink(TCHAR_TO_UTF8(*FullPipePath)); // Limpa o FIFO que foi criado
        return false;
    }
    bIsCreatedAndConnected = true;
    UE_LOG(LogIVR, Log, TEXT("FIFO '%s' opened successfully for writing."), *FullPipePath);

#else // Outras plataformas (se houver)
    UE_LOG(LogIVR, Error, TEXT("FIVR_PipeWrapper::Create not implemented for this platform."));
    return false;
#endif

    return bIsCreatedAndConnected;

}

// NOVO MÉTODO DE CONEXÃO
bool FIVR_PipeWrapper::Connect()
{
#if PLATFORM_WINDOWS
    if (!IsValid())
    {
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Cannot connect. Pipe handle is invalid for '%s'."), *FullPipePath);
        return false;
    }

    UE_LOG(LogIVRPipeWrapper, Log, TEXT("Awaiting client connection for pipe: %s"), *FullPipePath);

    // ConnectNamedPipe bloqueará até que um cliente se conecte.
    // ERROR_PIPE_CONNECTED significa que o pipe já estava conectado (em caso de chamadas sucessivas),
    // o que é um sucesso.
    BOOL bSuccess = ConnectNamedPipe(PipeHandle, NULL);
    if (!bSuccess && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        DWORD ErrorCode = GetLastError();
        UE_LOG(LogIVRPipeWrapper, Error, TEXT("Failed to connect Named Pipe '%s'. Error: %d"), *FullPipePath, ErrorCode);
        CloseHandle(PipeHandle);
        PipeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    UE_LOG(LogIVRPipeWrapper, Log, TEXT("Named Pipe '%s' connected successfully."), *FullPipePath);
    return true;
#else
    UE_LOG(LogIVRPipeWrapper, Warning, TEXT("Connect() not explicitly implemented for non-Windows platforms. FIFOs POSIX connect via open(). Assuming connection if Create() was successful."));
    return true; // Para POSIX, open() já bloqueia se não for NOWAIT, então consideramos conectado após Create().
#endif
}


// Write to Pipe
int32 FIVR_PipeWrapper::Write(const uint8* Data, int32 NumBytes)
{
    if (!IsValid())
    {
        UE_LOG(LogIVR, Error, TEXT("Attempted to write to an invalid or uninitialized pipe."));
        return -1;
    }

#if PLATFORM_WINDOWS
    DWORD BytesWritten;
    if (!WriteFile(PipeHandle, Data, NumBytes, &BytesWritten, nullptr))
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to write to Windows Named Pipe. Error: %d"), GetLastError());
        return -1;
    }

    if (BytesWritten != (DWORD)NumBytes)
    {
        UE_LOG(LogIVRPipeWrapper, Warning, TEXT("Incomplete write to Named Pipe '%s'. Wrote %d bytes."), *FullPipePath, BytesWritten);
    }

    return (int32)BytesWritten;

#elif PLATFORM_LINUX || PLATFORM_MAC
    ssize_t BytesWritten = write(FileDescriptor, Data, NumBytes);
    if (BytesWritten == -1)
    {
        UE_LOG(LogIVR, Error, TEXT("Failed to write to FIFO. Error: %s"), UTF8_TO_TCHAR(strerror(errno)));
        return -1;
    }
    return (int32)BytesWritten;

#else // Outras plataformas
    UE_LOG(LogIVR, Error, TEXT("FIVR_PipeWrapper::Write not implemented for this platform."));
    return -1;
#endif

}

// Close Pipe
void FIVR_PipeWrapper::Close()
{
    if (!IsValid())
    {
        return; // Já fechado ou nunca foi válido
    }

#if PLATFORM_WINDOWS
    if (PipeHandle != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(PipeHandle); // Garante que todos os dados sejam gravados
        DisconnectNamedPipe(PipeHandle); // Desconecta o cliente
        CloseHandle(PipeHandle);         // Fecha o handle
        PipeHandle = INVALID_HANDLE_VALUE;
        UE_LOG(LogIVR, Log, TEXT("Windows Named Pipe '%s' closed."), *FullPipePath);
    }
#elif PLATFORM_LINUX || PLATFORM_MAC
    if (FileDescriptor != -1)
    {
        close(FileDescriptor); // Fecha o descritor de arquivo
        FileDescriptor = -1;
        UE_LOG(LogIVR, Log, TEXT("FIFO '%s' file descriptor closed."), *FullPipePath);
    }
    // Remove o arquivo FIFO do sistema de arquivos
    if (!FullPipePath.IsEmpty() && access(TCHAR_TO_UTF8(*FullPipePath), F_OK) == 0)
    {
        unlink(TCHAR_TO_UTF8(*FullPipePath));
        UE_LOG(LogIVR, Log, TEXT("FIFO file '%s' unlinked."), *FullPipePath);
    }
#else // Outras plataformas
    // No-op
#endif
    bIsCreatedAndConnected = false;
    FullPipePath = TEXT(""); // Limpa o caminho
}

bool FIVR_PipeWrapper::IsValid() const
{
#if PLATFORM_WINDOWS
    return PipeHandle != INVALID_HANDLE_VALUE && bIsCreatedAndConnected;
#elif PLATFORM_LINUX || PLATFORM_MAC
    return FileDescriptor != -1 && bIsCreatedAndConnected;
#else // Outras plataformas
    return false;
#endif
}

FString FIVR_PipeWrapper::GetFullPipeName() const
{
    return FullPipePath;
}