# Requires -Version 5.1 # Compatível com Windows PowerShell 5.1 e superiores (incluindo PowerShell Core)

<#
.SYNOPSIS
    Converte arquivos de código-fonte para a codificação UTF-8 sem Byte Order Mark (BOM).

.DESCRIPTION
    Este script varre um diretório especificado (e seus subdiretórios)
    em busca de arquivos com as extensões fornecidas e os converte
    para a codificação UTF-8 sem BOM. Ele é útil para resolver
    warnings como o C4828 (caracteres inválidos na página de código)
    causados por problemas de codificação de arquivos.
    Um backup (.bak) de cada arquivo original é criado antes da modificação.

.PARAMETER RootPath
    O caminho do diretório raiz a partir do qual a varredura de arquivos será iniciada.
    Se não for especificado, o diretório onde este script está sendo executado será usado.

.PARAMETER FileExtensions
    Uma lista separada por vírgulas de extensões de arquivo a serem processadas
    (ex: "*.h", "*.cpp", "*.txt", "*.cs"). Use o asterisco como curinga.
    O padrão é "*.h", "*.cpp".

.PARAMETER WhatIf
    Executa o script em modo de simulação. Ele mostrará quais arquivos seriam processados
    e quais ações seriam tomadas, mas sem realmente modificar nenhum arquivo no disco.
    É altamente recomendado usar -WhatIf antes de executar o script de fato.

.EXAMPLE
    # Converte todos os arquivos .h e .cpp no diretório atual e seus subdiretórios
    .\Convert-FilesToUTF8NoBOM.ps1

.EXAMPLE
    # Converte arquivos .h e .cpp em um diretório específico (ex: C:\MeuProjeto\Source)
    .\Convert-FilesToUTF8NoBOM.ps1 -RootPath "C:\MeuProjeto\Source"

.EXAMPLE
    # Converte arquivos .txt e .xml em um diretório específico
    .\Convert-FilesToUTF8NoBOM.ps1 -RootPath "D:\Dados" -FileExtensions "*.txt", "*.xml"

.EXAMPLE
    # Simula a execução para ver o que seria feito, sem fazer alterações reais
    .\Convert-FilesToUTF8NoBOM.ps1 -WhatIf
#>
[CmdletBinding(SupportsShouldProcess=$true, ConfirmImpact='Medium')]
param (
    [Parameter(Mandatory=$false)]
    [string]$RootPath = $PSScriptRoot,

    [Parameter(Mandatory=$false)]
    [string[]]$FileExtensions = @("*.h", "*.cpp")
)

# --- Variáveis de Contadores ---
$FilesFound = 0
$FilesConverted = 0
$SkippedFiles = 0
$ErrorsEncountered = 0

# --- Validação do Caminho Raiz ---
if (-not (Test-Path $RootPath -PathType Container)) {
    Write-Error "Erro: O diretório raiz '$RootPath' não existe. Por favor, verifique o caminho fornecido."
    exit 1
}

Write-Host "Iniciando varredura e conversão de arquivos para UTF-8 sem BOM em '$RootPath'..."
Write-Host "Extensões a serem processadas: $($FileExtensions -join ', ')"
Write-Host "Serão criados backups com a extensão '.bak' para cada arquivo modificado."
Write-Host "" # Linha em branco para melhor leitura

# --- Processamento de Arquivos ---
try {
    # Obtém todos os arquivos que correspondem às extensões, de forma recursiva
    $Files = Get-ChildItem -Path $RootPath -Recurse -Include $FileExtensions -File -ErrorAction Stop

    foreach ($File in $Files) {
        $FilesFound++
        $FilePath = $File.FullName

        # Usa ShouldProcess para -WhatIf e -Confirm (se usado)
        if ($PSCmdlet.ShouldProcess($FilePath, "Converter para UTF-8 sem BOM")) {
            Write-Host "Processando: $FilePath"

            try {
                # 1. Criar um backup do arquivo original
                $BackupFilePath = $FilePath + ".bak"
                if ($PSCmdlet.ShouldProcess($BackupFilePath, "Criar backup")) {
                    Copy-Item -Path $FilePath -Destination $BackupFilePath -Force -ErrorAction Stop
                    Write-Host "  Backup criado em: $BackupFilePath"
                } else {
                    $SkippedFiles++
                    Write-Host "  Processamento de '$FilePath' ignorado devido à recusa do backup."
                    continue # Pula este arquivo se o backup não for permitido (ex: pelo usuário no -Confirm)
                }

                # 2. Ler o conteúdo inteiro do arquivo como uma única string.
                # É importante usar -Raw para manter as quebras de linha originais
                # e -Encoding UTF8 para que o PowerShell interprete corretamente
                # caracteres de arquivos que já podem estar em UTF-8 (com ou sem BOM)
                # ou outras codificações que possam ser lidas como UTF8.
                $FileContent = Get-Content -Path $FilePath -Raw -Encoding UTF8 -ErrorAction Stop

                # 3. Escrever o conteúdo de volta para o arquivo como UTF-8 sem BOM.
                # A classe [System.Text.Encoding]::UTF8, quando usada com WriteAllText,
                # por padrão, não adiciona o Byte Order Mark (BOM), o que é o objetivo.
                [System.IO.File]::WriteAllText($FilePath, $FileContent, [System.Text.Encoding]::UTF8)

                Write-Host "  Convertido com sucesso para UTF-8 sem BOM: $FilePath"
                $FilesConverted++

            }
            catch {
                Write-Warning "Erro ao processar '$FilePath': $($_.Exception.Message)"
                $ErrorsEncountered++
            }
        } else {
            $SkippedFiles++ # Contagem para arquivos pulados por -WhatIf ou -Confirm
        }
    }
}
catch {
    Write-Error "Ocorreu um erro inesperado ao listar os arquivos: $($_.Exception.Message)"
    $ErrorsEncountered++
}

# --- Resumo Final ---
Write-Host "`n--- Resumo do Processo ---"
Write-Host "Total de arquivos encontrados para varredura: $FilesFound"
Write-Host "Arquivos convertidos para UTF-8 sem BOM: $FilesConverted"
Write-Host "Arquivos ignorados/não processados (ex: por -WhatIf ou erro): $SkippedFiles"
Write-Host "Erros encontrados: $ErrorsEncountered"

if ($ErrorsEncountered -gt 0) {
    Write-Warning "Foram encontrados erros durante o processo. Por favor, revise as mensagens de aviso acima."
} else {
    Write-Host "Processo concluído sem erros."
}
