# TCC

## Limitação

Uso do módulo de WiFi ao invés do LENA-NB-IoT, porque não foi possível implementar o handover nele.

## Requisitos

- ns3.32
- sumo 1.23.1

---

1. Baixe o ns3.32
2. Descompacte o arquivo
3. Instale as bibliotecas necessárias destacadas [neste vídeo](https://www.youtube.com/watch?v=xE1jUh3-mOI)
4. Execute o `./build.py`
5. Execute o `./waf` com o comando de configuração presente no arquivo `runner.py`
6. Execute as simulações usando `./runner.py`

> [!IMPORTANT]
> Exporte a variável de ambiente `LD_LIBRARY_PATH` com o caminho do diretório `build/lib` para evitar problemas.

## Pendências

- [x] Parâmetro para **Quantidade de Nós** 
- [x] Parâmetro para **Frequência de Coleta de Dados de Rastreamento** 
- [ ] Adiciona handover entre pontos
- [ ] Adicionar modelo de energia
  - [ ] PSM, DRX e eDRX (Constantes a cada x segundos)
  - [ ] Uplink (Envio)
  - [ ] Downlink (Recebimento)
  - [ ] Repouso
  - [ ] PSM do GPS

## Observações

- Apenas rodar o *script* através do `runner.py`, pois ele já utiliza a versão de *build* optimizada do código

## Scripts

**Automatização do Arquivo de Mobilidade NS2**

```sh
make -e end_time=END_TIME period=PERIOD min_distance=MIN_DISTANCE
```

**Geração de Veículos Aleatórios a Partir de uma Rede**

```sh
python3 randomTrips.py -n NET_FILE -e 50000 -p 2.50 --min-distance 600.0 -r ROUTE_FILE
```

**Criar Arquivo de Trace**

```sh
sumo -c SUMO_CONFIG_FILE --fcd-output TRACE_FILE
```

**Converter Arquivo de Trace para Arquivo de Mobilidade NS2**

```sh
python3 traceExporter.py --fcd-input TRACE_FILE --ns2mobility-output MOBILITY_FILE
```

## Referências

- [Como instalar ns3.32 no Ubuntu 20.04](https://www.youtube.com/watch?v=xE1jUh3-mOI)
