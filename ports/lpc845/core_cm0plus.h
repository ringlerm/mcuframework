#ifndef __CORE_CM0PLUS_H_GENERIC__
#define __CORE_CM0PLUS_H_GENERIC__

#include <stdint.h>

/* Atributos de permisos de registros estándar de ARM */
#define __I     volatile const       /*!< Permiso de solo lectura */
#define __O     volatile             /*!< Permiso de solo escritura */
#define __IO    volatile             /*!< Permiso de lectura/escritura */
#define __IM    volatile const
#define __OM    volatile
#define __IOM   volatile

#define __STATIC_INLINE static inline

/* Funciones intrínsecas del núcleo Cortex-M0+ (GCC inline assembly) */
__STATIC_INLINE void __enable_irq(void)  { __asm volatile ("cpsie i" : : : "memory"); }
__STATIC_INLINE void __disable_irq(void) { __asm volatile ("cpsid i" : : : "memory"); }
__STATIC_INLINE void __NOP(void)         { __asm volatile ("nop"); }
__STATIC_INLINE void __WFI(void)         { __asm volatile ("wfi"); }
__STATIC_INLINE void __WFE(void)         { __asm volatile ("wfe"); }
__STATIC_INLINE void __ISB(void)         { __asm volatile ("isb 0xF":::"memory"); }
__STATIC_INLINE void __DSB(void)         { __asm volatile ("dsb 0xF":::"memory"); }
__STATIC_INLINE void __DMB(void)         { __asm volatile ("dmb 0xF":::"memory"); }

/* Estructura del SysTick (Requerida por capas de delay/soft_timers) */
typedef struct {
  __IOM uint32_t CTRL;                   /*!< Offset: 0x000 (R/W)  SysTick Control and Status Register */
  __IOM uint32_t LOAD;                   /*!< Offset: 0x004 (R/W)  SysTick Reload Value Register */
  __IOM uint32_t VAL;                    /*!< Offset: 0x008 (R/W)  SysTick Current Value Register */
  __IM  uint32_t CALIB;                  /*!< Offset: 0x00C (R/ )  SysTick Calibration Register */
} SysTick_Type;

#define SysTick_BASE        (0xE000E010UL)
#define SysTick             ((SysTick_Type   *)     SysTick_BASE     )

/* Estructura del NVIC (Controlador de Interrupciones) */
typedef struct {
  __IOM uint32_t ISER[1U];               /*!< Offset: 0x000 (R/W)  Interrupt Set Enable Register */
        uint32_t RESERVED0[31U];
  __IOM uint32_t ICER[1U];               /*!< Offset: 0x080 (R/W)  Interrupt Clear Enable Register */
        uint32_t RSERVED1[31U];
  __IOM uint32_t ISPR[1U];               /*!< Offset: 0x100 (R/W)  Interrupt Set Pending Register */
        uint32_t RESERVED2[31U];
  __IOM uint32_t ICPR[1U];               /*!< Offset: 0x180 (R/W)  Interrupt Clear Pending Register */
        uint32_t RESERVED3[31U];
        uint32_t RESERVED4[64U];
  __IOM uint32_t IP[8U];                 /*!< Offset: 0x400 (R/W)  Interrupt Priority Register */
}  NVIC_Type;

#define NVIC_BASE           (0xE000E100UL)
#define NVIC                ((NVIC_Type      *)     NVIC_BASE        )

#endif /* __CORE_CM0PLUS_H_GENERIC__ */
