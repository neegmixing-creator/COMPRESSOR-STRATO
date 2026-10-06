# Compilar o Compressor VST3 no GitHub Actions

Este projeto inclui um workflow em `.github/workflows/build-vst3.yml`.

## Como usar

1. Crie um repositório no GitHub.
2. Envie todos os arquivos deste projeto para o repositório.
3. Abra a aba **Actions**.
4. Entre em **Build VST3**.
5. Clique em **Run workflow** para iniciar manualmente, ou faça um novo push.
6. Quando terminar, abra a execução concluída.
7. Na seção **Artifacts**, baixe `CompressorPlugin-VST3-Windows`.

O workflow usa um runner Windows hospedado pelo GitHub, portanto o compilador MSVC não precisa ser instalado no seu PC.

O build também executa os testes do núcleo DSP antes de publicar o artefato.
