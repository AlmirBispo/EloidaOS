# 🌿 ELOIDA OS

**Sistema Operacional minimalista baseado em números primos para economia extrema de recursos**

> *"Menos é mais: quando cada ciclo de CPU conta, a matemática dos primos se torna seu scheduler."*

![RISC-V](https://img.shields.io/badge/ISA-RISC--V-green)
![MCU](https://img.shields.io/badge/MCU-CH32V307VCT6-blue)
![License](https://img.shields.io/badge/License-MIT-yellow)
![Version](https://img.shields.io/badge/Version-1.0-orange)

**Autor:** Almir Bispo · **Plataforma:** RISC-V (CH32V307VCT6) · **Data:** 22/01/2026

---

## 🎯 O que é o ELOIDA OS?

O **ELOIDA OS** é um kernel cooperativo *bare-metal* projetado para microcontroladores com recursos restritos. Sua inovação central é o uso de **números primos como contadores de despacho**, garantindo:

- ✅ **Ausência de colisões periódicas** entre tarefas (devido à coprimalidade)
- ✅ **Zero alocação dinâmica** de memória
- ✅ **Zero overhead** de troca de contexto (despacho por tabela estática)
- ✅ **IPC por memória compartilhada** sem necessidade de mutex (cooperativo)

## 📚 Documentação

Acesse a documentação completa em HTML no dubdiretorio



## ⚡ Quick Start

```c
// Cada tarefa é uma função void(void) registrada na tabela
void minha_task(void) { /* sua lógica */ }

// O main loop se encarrega do despacho automático
// usando números primos para i < 3 e (1000+i) para demais
```

## 🤝 Contribuições

Contribuições são bem-vindas! Leia [expansao.html](docs/expansao.html) antes de abrir um PR.

## 📄 Licença

Distribuído sob a licença MIT. Veja `LICENSE`.
