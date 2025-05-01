# TCC

## Requisitos

- ns3.32

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
- [ ] Parâmetro para **Cobertura de Rede** 
- [ ] Parâmetro para **Tamanho do Payload** 
- [x] Parâmetro para **Modo de Transmissão** 
- [ ] Parâmetro para **Frequência de Coleta de Dados de Rastreamento** 
- [ ] Realizar o cálculo da cobertura de rede considerando torres estáticas 

## Observações

- Apenas rodar o *script* através do `runner.py`, pois ele já utiliza a versão de *build* optimizada do código

## Referências

- [Como instalar ns3.32 no Ubuntu 20.04](https://www.youtube.com/watch?v=xE1jUh3-mOI)
