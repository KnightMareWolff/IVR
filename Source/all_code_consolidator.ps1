# Nome do arquivo de saída onde todo o código será consolidado.
$outputFile = "all_code.txt"

# Extensões dos arquivos de código a serem incluídos.
$extensions = @(".h", ".cpp" , ".cs")

Write-Host "✅ Iniciando a consolidação de arquivos .h e .cpp para Williäm Wolff..." -ForegroundColor Green
Write-Host "O arquivo de saída será: '$outputFile'." -ForegroundColor DarkYellow
Write-Host "Verificando e removendo qualquer versão anterior de '$outputFile' para garantir um início limpo." -ForegroundColor DarkYellow

# --- Tratamento de Erros e Inicialização ---
try {
    # Remove o arquivo de saída existente para garantir um início limpo.
    # O parametro -ErrorAction SilentlyContinue evita que o script pare se o arquivo não existir.
    # O parametro -Force permite remover arquivos somente leitura, se houver.
    Remove-Item $outputFile -ErrorAction SilentlyContinue -Force

    # Cria o arquivo de saída vazio com a codificação UTF-8 para garantir que todas as escritas posteriores mantenham esta codificação.
    "" | Set-Content -Path $outputFile -Encoding UTF8 -ErrorAction Stop
    Write-Host "Arquivo '$outputFile' criado (ou redefinido) com codificação UTF-8." -ForegroundColor Green

    Write-Host "Buscando arquivos com as extensões $($extensions -join ', ') no diretório atual e subpastas..." -ForegroundColor Cyan

    # --- Localização e Seleção de Arquivos ---
    # Get-ChildItem -Recurse: Varre o diretório atual e todas as subpastas.
    # -File: Garante que apenas arquivos (e não diretórios) sejam retornados.
    # Where-Object: Filtra os arquivos para incluir apenas aqueles com as extensões desejadas.
    $files = Get-ChildItem -Path . -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $extensions -contains $_.Extension }

    if (-not $files) {
        Write-Warning "Nenhum arquivo com as extensões $($extensions -join ', ') encontrado no diretório atual ou subpastas. O script será encerrado."
        exit
    }

    $fileCount = $files.Count
    $processedCount = 0

    Write-Host "Total de arquivos encontrados: $fileCount." -ForegroundColor Green

    # --- Consolidação de Conteúdo ---
    foreach ($file in $files) {
        $processedCount++
        $relativeFilePath = Resolve-Path -Path $file.FullName -Relative # Caminho relativo para o output mais limpo
        Write-Host "Processando arquivo ($processedCount/$fileCount): $relativeFilePath" -ForegroundColor Cyan

        try {
            # --- Formato do Arquivo de Saída ---
            $header = "--- INÍCIO DO ARQUIVO: $($file.FullName) ---"
            $footer = "--- FIM DO ARQUIVO: $($file.FullName) ---"
            $separator = "`n`n" # Duas linhas vazias para uma separação clara entre os arquivos

            # Lê o conteúdo do arquivo. -Raw lê o arquivo inteiro como uma única string.
            # -Encoding UTF8 é importante para ler corretamente arquivos que podem conter caracteres especiais.
            $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8 -ErrorAction Stop

            # Adiciona o cabeçalho, conteúdo, rodapé e separador ao arquivo de saída.
            # Add-Content -Encoding UTF8 garante que a codificação seja mantida.
            Add-Content -Path $outputFile -Value $header -Encoding UTF8
            Add-Content -Path $outputFile -Value $content -Encoding UTF8
            Add-Content -Path $outputFile -Value $footer -Encoding UTF8
            Add-Content -Path $outputFile -Value $separator -Encoding UTF8

        }
        catch [System.IO.IOException] {
            # Trata erros de E/S, como arquivo sendo usado por outro processo ou problemas de permissão para ler.
            Write-Warning "   Erro de E/S ao processar '$($file.FullName)': $($_.Exception.Message)"
        }
        catch [System.UnauthorizedAccessException] {
            # Trata especificamente erros de permissão para ler o arquivo.
            Write-Warning "⚠️ Erro de permissão ao acessar '$($file.FullName)': $($_.Exception.Message)"
        }
        catch {
            # Captura quaisquer outros erros inesperados durante o processamento de um arquivo individual.
            Write-Warning "🚫 Erro inesperado ao processar '$($file.FullName)': $($_.Exception.Message)"
        }
    }

    Write-Host "✅ Consolidação concluída! O conteúdo combinado está em '$outputFile'." -ForegroundColor Green
    Write-Host "Este arquivo está pronto para ser usado para análise por um modelo de IA." -ForegroundColor Green

}
catch [System.UnauthorizedAccessException] {
    # Trata erros de permissão para criar ou modificar o arquivo de saída.
    Write-Error "❌ Erro fatal de permissão: Você não tem as permissões necessárias para criar ou gravar no arquivo '$outputFile'. Verifique as permissões do diretório e tente novamente. $($_.Exception.Message)"
}
catch [System.Exception] {
    # Captura quaisquer outros erros fatais que possam ocorrer durante a execução geral do script.
    Write-Error "❌ Ocorreu um erro inesperado e fatal durante a execução do script: $($_.Exception.Message)"
}