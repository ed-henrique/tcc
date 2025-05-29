# TCC

## Requisitos

- ns3.32
- sumo 1.23.1

---

1. Baixe o ns3.32
2. Descompacte o arquivo
3. Clone o repositório [LENA-NB](https://github.com/tudo-cni/ns3-lena-nb) para substituir a pasta `lte` do ns3.32
4. Clone o repositório [Winner+ Channel](https://github.com/tudo-cni/ns3-propagation-winner-plus) para substituir a pasta `propagation` do ns3.32
5. Instale as bibliotecas necessárias destacadas [neste vídeo](https://www.youtube.com/watch?v=xE1jUh3-mOI)
6. Execute o `./build.py`
7. Execute o `./waf` com o comando de configuração presente no arquivo `runner.py`
8. Execute as simulações usando `./runner.py`

## Pendências

- [x] Parâmetro para **Quantidade de nós** 
- [x] Parâmetro para **Tamanho do Payload** 
- [x] Parâmetro para **Modo de Transmissão** 
- [x] Parâmetro para **Frequência de Coleta de Dados de Rastreamento** 

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
