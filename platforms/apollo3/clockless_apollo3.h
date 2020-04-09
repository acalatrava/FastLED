#ifndef __INC_CLOCKLESS_APOLLO3_H
#define __INC_CLOCKLESS_APOLLO3_H

FASTLED_NAMESPACE_BEGIN

#if defined(FASTLED_APOLLO3)

#define FASTLED_HAS_CLOCKLESS 1

#define DO_RGBW // Uncomment this line to enable support for (e.g.) SK6812RGBW that need extra white bits

#ifdef DO_RGBW
// Set all the white LEDs to this level. (What were you expecting for free!? ;-)
#define WHITE_LEVEL 1
#endif

template <int DATA_PIN, int T1, int T2, int T3, EOrder RGB_ORDER = RGB, int XTRA0 = 0, bool FLIP = false, int WAIT_TIME = 50>
class ClocklessController : public CPixelLEDController<RGB_ORDER> {
	typedef typename FastPin<DATA_PIN>::port_ptr_t data_ptr_t;
	typedef typename FastPin<DATA_PIN>::port_t data_t;

  CMinWait<WAIT_TIME> mWait;

public:
	virtual void init() {
		// Initialize everything
		// This is _very_ SparkFun Artemis / Ambiq Micro Apollo3 Blue specific!

		// Configure DATA_PIN for FastGPIO (settings are in fastpin_apollo3.h)
		FastPin<DATA_PIN>::setOutput();
    FastPin<DATA_PIN>::lo();

		// Make sure the system clock is running at the full 48MHz
	  am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_SYSCLK_MAX, 0);

	  // Make sure interrupts are enabled
	  am_hal_interrupt_master_enable();

	  // Enable SysTick Interrupts in the NVIC
	  NVIC_EnableIRQ(SysTick_IRQn);

		// SysTick is 24-bit and counts down (not up)

	  // Stop the SysTick (just in case it is already running).
	  // This clears the ENABLE bit in the SysTick Control and Status Register (SYST_CSR).
	  // In Ambiq naming convention: the control register is SysTick->CTRL
	  am_hal_systick_stop();

	  // Call SysTick_Config
	  // This is defined in core_cm4.h
	  // It loads the specified LOAD value into the SysTick Reload Value Register (SYST_RVR)
	  // In Ambiq naming convention: the reload register is SysTick->LOAD
	  // It sets the SysTick interrupt priority
	  // It clears the SysTick Current Value Register (SYST_CVR)
	  // In Ambiq naming convention: the current value register is SysTick->VAL
	  // Finally it sets these bits in the SysTick Control and Status Register (SYST_CSR):
	  // CLKSOURCE: SysTick uses the processor clock
	  // TICKINT: When the count reaches zero, the SysTick exception (interrupt) is changed to pending
	  // ENABLE: Enables the counter
	  // SysTick_Config returns 0 if successful. 1 indicates a failure (the LOAD value was invalid).
	  SysTick_Config(0xFFFFFFUL); // The LOAD value needs to be 24-bit
	}

	virtual uint16_t getMaxRefreshRate() const { return 400; } // This can probably be increased?

	static void SysTick_Handler(void) {
		// We don't actually need to do anything in the ISR. There just needs to be one!
	}

protected:

	virtual void showPixels(PixelController<RGB_ORDER> & pixels) {
    mWait.wait();
		if(!showRGBInternal(pixels)) {
      sei(); delayMicroseconds(WAIT_TIME); cli();
      showRGBInternal(pixels);
    }
    mWait.mark();
  }

	// SysTick counts down not up and is 24-bit, so let's ex-or it so it appears to count up
	#define am_hal_systick_count_inverted() (am_hal_systick_count() ^ 0xFFFFFF)

	template<int BITS> __attribute__ ((always_inline)) inline static void writeBits(register uint32_t & next_mark, register uint8_t & b)  {
		// SysTick counts down (not up) and is 24-bit
		for(register uint32_t i = BITS-1; i > 0; i--) { // We could speed this up by using Bit Banding
      while(am_hal_systick_count_inverted() < next_mark);
      next_mark = (am_hal_systick_count_inverted() + T1+T2+T3) & 0xFFFFFF;
			// (This will glitch when next_mark would normally exceed 0xFFFFFF)
      FastPin<DATA_PIN>::hi();
			if(b&0x80) {
        while((next_mark - am_hal_systick_count_inverted()) > (T3));//+(2*(F_CPU/24000000))));
        FastPin<DATA_PIN>::lo();
			} else {
        while((next_mark - am_hal_systick_count_inverted()) > (T2+T3));//+(2*(F_CPU/24000000))));
        FastPin<DATA_PIN>::lo();
			}
			b <<= 1;
		}

    while(am_hal_systick_count_inverted() < next_mark);
    next_mark = (am_hal_systick_count_inverted() + T1+T2+T3) & 0xFFFFFF;
		// (This will glitch when next_mark would normally exceed 0xFFFFFF)
    FastPin<DATA_PIN>::hi();
    if(b&0x80) {
      while((next_mark - am_hal_systick_count_inverted()) > (T3));//+(2*(F_CPU/24000000))));
      FastPin<DATA_PIN>::lo();
    } else {
      while((next_mark - am_hal_systick_count_inverted()) > (T2+T3));//+(2*(F_CPU/24000000))));
      FastPin<DATA_PIN>::lo();
    }
	}

	// This method is made static to force making register Y available to use for data on AVR - if the method is non-static, then
	// gcc will use register Y for the this pointer.
	static uint32_t showRGBInternal(PixelController<RGB_ORDER> pixels) {
    FastPin<DATA_PIN>::lo();

		// Setup the pixel controller and load/scale the first byte
		pixels.preStepFirstByteDithering();
		register uint8_t b = pixels.loadAndScale0();

		// The SysTick ISR appears not be working so let's manually reload the SysTick VAL
		//am_hal_systick_load(0xFFFFFF);

		cli();
		// Calculate next_mark (the time of the next DATA_PIN transition)
		// SysTick counts down (not up) and is 24-bit so let's use the inverted version and mask it to 24 bits
		// (This will glitch when next_mark would normally exceed 0xFFFFFF)
    register uint32_t next_mark = (am_hal_systick_count_inverted() + T1+T2+T3) & 0xFFFFFF;

		while(pixels.has(1)) {
			pixels.stepDithering();

			#if (FASTLED_ALLOW_INTERRUPTS == 1)
			cli();
			if(am_hal_systick_count_inverted() > next_mark) { // Have we already missed the next_mark?
				// If we have exceeded next_mark by an excessive amount, then bail (return 0)
				if((am_hal_systick_count_inverted() - next_mark) > ((WAIT_TIME-INTERRUPT_THRESHOLD)*CLKS_PER_US)) { sei(); return 0; }
			}
			#endif

			// Write first byte, read next byte
			writeBits<8+XTRA0>(next_mark, b);
			b = pixels.loadAndScale1();

			// Write second byte, read 3rd byte
			writeBits<8+XTRA0>(next_mark, b);
			b = pixels.loadAndScale2();

			// Write third byte, read 1st byte of next pixel
			writeBits<8+XTRA0>(next_mark, b);
			b = pixels.advanceAndLoadAndScale0();

			// Write the extra white bits if the RGBW strip needs them
			#ifdef DO_RGBW
			register uint8_t white_level = WHITE_LEVEL;
			writeBits<8+XTRA0>(next_mark, white_level);
			#endif

			#if (FASTLED_ALLOW_INTERRUPTS == 1)
			sei();
			#endif
		};

		sei();
		return (am_hal_systick_count_inverted());
	}

};


#endif

FASTLED_NAMESPACE_END

#endif
