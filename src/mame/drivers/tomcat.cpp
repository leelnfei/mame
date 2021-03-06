// license:BSD-3-Clause
// copyright-holders:Mariusz Wojcieszek
/*

    Atari Tomcat prototype hardware

    Driver by Mariusz Wojcieszek

    Notes:
    - game has no sound, while sound hardware was developed, sound program was
      not prepared

    TODO:
    - add proper timing of interrupts and framerate (currently commented out,
      as they cause test mode to hang)
    - vector quality appears to be worse than original game (compared to original
      game videos)
    - verify controls
    - implement game linking (after MAME supports network)
    - current implementation of 68010 <-> tms32010 is a little bit hacky, after
      tms32010 is started by 68010, 68010 is suspended until tms32010 reads command
      and starts executing
    - hook up tms5220 - it is currently not used at all

*/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "cpu/tms32010/tms32010.h"
#include "cpu/m6502/m6502.h"
#include "video/vector.h"
#include "video/avgdvg.h"
#include "machine/74259.h"
#include "machine/adc0808.h"
#include "machine/timekpr.h"
#include "machine/nvram.h"
#include "machine/watchdog.h"
#include "machine/6532riot.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"
#include "sound/ym2151.h"
#include "screen.h"
#include "speaker.h"



class tomcat_state : public driver_device
{
public:
	tomcat_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_tms(*this, "tms"),
		m_shared_ram(*this, "shared_ram"),
		m_maincpu(*this, "maincpu"),
		m_dsp(*this, "dsp"),
		m_adc(*this, "adc"),
		m_mainlatch(*this, "mainlatch")
	{ }

	void tomcat(machine_config &config);

private:
	DECLARE_WRITE8_MEMBER(adcon_w);
	DECLARE_READ16_MEMBER(tomcat_inputs_r);
	DECLARE_WRITE16_MEMBER(main_latch_w);
	DECLARE_WRITE_LINE_MEMBER(lnkmode_w);
	DECLARE_WRITE_LINE_MEMBER(err_w);
	DECLARE_WRITE_LINE_MEMBER(ack_w);
	DECLARE_WRITE_LINE_MEMBER(txbuff_w);
	DECLARE_WRITE_LINE_MEMBER(sndres_w);
	DECLARE_WRITE_LINE_MEMBER(mres_w);
	DECLARE_WRITE16_MEMBER(tomcat_irqclr_w);
	DECLARE_READ16_MEMBER(tomcat_inputs2_r);
	DECLARE_READ16_MEMBER(tomcat_320bio_r);
	DECLARE_READ8_MEMBER(tomcat_nvram_r);
	DECLARE_WRITE8_MEMBER(tomcat_nvram_w);
	DECLARE_READ_LINE_MEMBER(dsp_BIO_r);
	DECLARE_WRITE8_MEMBER(soundlatches_w);
	virtual void machine_start() override;
	void dsp_map(address_map &map);
	void sound_map(address_map &map);
	void tomcat_map(address_map &map);

	required_device<tms5220_device> m_tms;
	required_shared_ptr<uint16_t> m_shared_ram;
	uint8_t m_nvram[0x800];
	int m_dsp_BIO;
	int m_dsp_idle;

	required_device<cpu_device> m_maincpu;
	required_device<cpu_device> m_dsp;
	required_device<adc0808_device> m_adc;
	required_device<ls259_device> m_mainlatch;
};



WRITE8_MEMBER(tomcat_state::adcon_w)
{
	m_adc->address_w(space, 0, data & 7);
	m_adc->start_w(BIT(data, 3));
}

READ16_MEMBER(tomcat_state::tomcat_inputs_r)
{
	uint16_t result = 0;
	if (ACCESSING_BITS_8_15)
		result |= ioport("IN0")->read() << 8;

	return result;
}

WRITE16_MEMBER(tomcat_state::main_latch_w)
{
	// A1-A3 = address, A4 = data
	m_mainlatch->write_bit(offset & 7, BIT(offset, 3));
}

WRITE_LINE_MEMBER(tomcat_state::lnkmode_w)
{
	// Link Mode
	// When Low: Master does not respond to Interrupts
}

WRITE_LINE_MEMBER(tomcat_state::err_w)
{
	// Link Error Flag
}

WRITE_LINE_MEMBER(tomcat_state::ack_w)
{
	// Link ACK Flag
}

WRITE_LINE_MEMBER(tomcat_state::txbuff_w)
{
	// Link Buffer Control
	// When High: Turn off TX (Link) Buffer
}

WRITE_LINE_MEMBER(tomcat_state::sndres_w)
{
	// Sound Reset
	// When Low: Reset Sound System
	// When High: Release reset of sound system
}

WRITE_LINE_MEMBER(tomcat_state::mres_w)
{
	// 320 Reset
	// When Low: Reset TMS320
	// When High: Release reset of TMS320
	if (state)
		m_dsp_BIO = 0;
	m_dsp->set_input_line(INPUT_LINE_RESET, state ? CLEAR_LINE : ASSERT_LINE);
}

WRITE16_MEMBER(tomcat_state::tomcat_irqclr_w)
{
	// Clear IRQ Latch          (Address Strobe)
	m_maincpu->set_input_line(1, CLEAR_LINE);
}

READ16_MEMBER(tomcat_state::tomcat_inputs2_r)
{
/*
*       D15 LNKFLAG     (Game Link)
*       D14 PC3        "    "
*       D13 PC2        "    "
*       D12 PC0        "    "
*       D11 MAINFLAG    (Sound System)
*       D10 SOUNDFLAG      "    "
*       D9  /IDLE*      (TMS320 System)
*       D8
*/
	return m_dsp_idle ? 0 : (1 << 9);
}

READ16_MEMBER(tomcat_state::tomcat_320bio_r)
{
	m_dsp_BIO = 1;
	m_maincpu->suspend(SUSPEND_REASON_SPIN, 1);
	return 0;
}

READ_LINE_MEMBER(tomcat_state::dsp_BIO_r)
{
	if ( m_dsp->pc() == 0x0001 )
	{
		if ( m_dsp_idle == 0 )
		{
			m_dsp_idle = 1;
			m_dsp_BIO = 0;
		}
		return !m_dsp_BIO;
	}
	else if ( m_dsp->pc() == 0x0003 )
	{
		if ( m_dsp_BIO == 1 )
		{
			m_dsp_idle = 0;
			m_dsp_BIO = 0;
			m_maincpu->resume(SUSPEND_REASON_SPIN );
			return 0;
		}
		else
		{
			assert(0);
			return 0;
		}
	}
	else
	{
		return !m_dsp_BIO;
	}
}

READ8_MEMBER(tomcat_state::tomcat_nvram_r)
{
	return m_nvram[offset];
}

WRITE8_MEMBER(tomcat_state::tomcat_nvram_w)
{
	m_nvram[offset] = data;
}

void tomcat_state::tomcat_map(address_map &map)
{
	map(0x000000, 0x00ffff).rom();
	map(0x402001, 0x402001).r("adc", FUNC(adc0808_device::data_r)).w(FUNC(tomcat_state::adcon_w));
	map(0x404000, 0x404001).r(FUNC(tomcat_state::tomcat_inputs_r)).w("avg", FUNC(avg_tomcat_device::go_word_w));
	map(0x406000, 0x406001).w("avg", FUNC(avg_tomcat_device::reset_word_w));
	map(0x408000, 0x408001).r(FUNC(tomcat_state::tomcat_inputs2_r)).w("watchdog", FUNC(watchdog_timer_device::reset16_w));
	map(0x40a000, 0x40a001).rw(FUNC(tomcat_state::tomcat_320bio_r), FUNC(tomcat_state::tomcat_irqclr_w));
	map(0x40e000, 0x40e01f).w(FUNC(tomcat_state::main_latch_w));
	map(0x800000, 0x803fff).ram().share("vectorram");
	map(0xffa000, 0xffbfff).ram().share("shared_ram");
	map(0xffc000, 0xffcfff).ram();
	map(0xffd000, 0xffdfff).rw("m48t02", FUNC(timekeeper_device::read), FUNC(timekeeper_device::write)).umask16(0xff00);
	map(0xffd000, 0xffdfff).rw(FUNC(tomcat_state::tomcat_nvram_r), FUNC(tomcat_state::tomcat_nvram_w)).umask16(0x00ff);
}

void tomcat_state::dsp_map(address_map &map)
{
	map(0x0000, 0x0fff).ram().share("shared_ram");
}


WRITE8_MEMBER(tomcat_state::soundlatches_w)
{
	switch(offset)
	{
		case 0x00: break; // XLOAD 0    Write the Sequential ROM counter Low Byte
		case 0x20: break; // XLOAD 1    Write the Sequential ROM counter High Byte
		case 0x40: break; // SOUNDWR    Write to Sound Interface Latch (read by Main)
		case 0x60: break; // unused
		case 0x80: break; // XREAD      Read the Sequential ROM (64K bytes) and increment the counter
		case 0xa0: break; // unused
		case 0xc0: break; // SOUNDREAD  Read the Sound Interface Latch (written by Main)
	}
}

void tomcat_state::sound_map(address_map &map)
{
	map(0x0000, 0x1fff).ram();
	map(0x2000, 0x2001).rw("ymsnd", FUNC(ym2151_device::read), FUNC(ym2151_device::write));
	map(0x3000, 0x30df).w(FUNC(tomcat_state::soundlatches_w));
	map(0x30e0, 0x30e0).noprw(); // COINRD Inputs: D7 = Coin L, D6 = Coin R, D5 = SOUNDFLAG
	map(0x5000, 0x507f).ram(); // 6532 ram
	map(0x5080, 0x509f).rw("riot", FUNC(riot6532_device::read), FUNC(riot6532_device::write));
	map(0x6000, 0x601f).rw("pokey1", FUNC(pokey_device::read), FUNC(pokey_device::write));
	map(0x7000, 0x701f).rw("pokey2", FUNC(pokey_device::read), FUNC(pokey_device::write));
	map(0x8000, 0xffff).noprw(); // main sound program rom
}

static INPUT_PORTS_START( tomcat )
	PORT_START("IN0")   /* INPUTS */
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_CUSTOM ) PORT_CUSTOM_MEMBER("avg", avg_tomcat_device, done_r, nullptr)
	PORT_BIT( 0x02, IP_ACTIVE_LOW,  IPT_UNUSED ) // SPARE
	PORT_BIT( 0x04, IP_ACTIVE_LOW,  IPT_BUTTON5 ) // DIAGNOSTIC
	PORT_SERVICE( 0x08, IP_ACTIVE_LOW )
	PORT_BIT( 0x10, IP_ACTIVE_LOW,  IPT_BUTTON2 ) // R FIRE
	PORT_BIT( 0x20, IP_ACTIVE_LOW,  IPT_BUTTON1 ) // L FIRE
	PORT_BIT( 0x40, IP_ACTIVE_LOW,  IPT_BUTTON4 ) // R THUMB
	PORT_BIT( 0x80, IP_ACTIVE_LOW,  IPT_BUTTON3 ) // L THUMB

	PORT_START("STICKY")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_Y ) PORT_SENSITIVITY(70) PORT_KEYDELTA(30)

	PORT_START("STICKX")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_X ) PORT_SENSITIVITY(50) PORT_KEYDELTA(30)
INPUT_PORTS_END

void tomcat_state::machine_start()
{
	((uint16_t*)m_shared_ram)[0x0000] = 0xf600;
	((uint16_t*)m_shared_ram)[0x0001] = 0x0000;
	((uint16_t*)m_shared_ram)[0x0002] = 0xf600;
	((uint16_t*)m_shared_ram)[0x0003] = 0x0000;

	subdevice<nvram_device>("nvram")->set_base(m_nvram, 0x800);

	save_item(NAME(m_nvram));
	save_item(NAME(m_dsp_BIO));
	save_item(NAME(m_dsp_idle));

	m_dsp_BIO = 0;
}

MACHINE_CONFIG_START(tomcat_state::tomcat)
	MCFG_DEVICE_ADD("maincpu", M68010, 12.096_MHz_XTAL / 2)
	MCFG_DEVICE_PROGRAM_MAP(tomcat_map)
	MCFG_DEVICE_PERIODIC_INT_DRIVER(tomcat_state, irq1_line_assert,  5*60)
	//MCFG_DEVICE_PERIODIC_INT_DRIVER(tomcat_state, irq1_line_assert, 12.096_MHz_XTAL / 16 / 16 / 16 / 12)

	MCFG_DEVICE_ADD("dsp", TMS32010, 16_MHz_XTAL)
	MCFG_DEVICE_PROGRAM_MAP( dsp_map)
	MCFG_TMS32010_BIO_IN_CB(READLINE(*this, tomcat_state, dsp_BIO_r))

	MCFG_DEVICE_ADD("soundcpu", M6502, 14.318181_MHz_XTAL / 8 )
	MCFG_DEVICE_DISABLE()
	MCFG_DEVICE_PROGRAM_MAP( sound_map)

	ADC0809(config, m_adc, 12.096_MHz_XTAL / 16);
	m_adc->in_callback<0>().set_ioport("STICKY");
	m_adc->in_callback<1>().set_ioport("STICKX");

	MCFG_DEVICE_ADD("riot", RIOT6532, 14.318181_MHz_XTAL / 8)
	/*
	 PA0 = /WS   OUTPUT  (TMS-5220 WRITE STROBE)
	 PA1 = /RS   OUTPUT  (TMS-5220 READ STROBE)
	 PA2 = /READY    INPUT   (TMS-5220 READY FLAG)
	 PA3 = FSEL  OUTPUT  Select TMS5220 clock;
	 0 = 325 KHz (8 KHz sampling)
	 1 = 398 KHz (10 KHz sampling)
	 PA4 = /CC1  OUTPUT  Coin Counter 1
	 PA5 = /CC2  OUTPUT  Coin Counter 2
	 PA6 = /MUSRES   OUTPUT  (Reset the Yamaha)
	 PA7 = MAINFLAG  INPUT
	 */
	// OUTB PB0 - PB7   OUTPUT  Speech Data
	// IRQ CB connected to IRQ line of 6502

	MCFG_QUANTUM_TIME(attotime::from_hz(4000))

	LS259(config, m_mainlatch);
	m_mainlatch->q_out_cb<0>().set_output("led1").invert();
	m_mainlatch->q_out_cb<1>().set_output("led2").invert();
	m_mainlatch->q_out_cb<2>().set(FUNC(tomcat_state::mres_w));
	m_mainlatch->q_out_cb<3>().set(FUNC(tomcat_state::sndres_w));
	m_mainlatch->q_out_cb<4>().set(FUNC(tomcat_state::lnkmode_w));
	m_mainlatch->q_out_cb<5>().set(FUNC(tomcat_state::err_w));
	m_mainlatch->q_out_cb<6>().set(FUNC(tomcat_state::ack_w));
	m_mainlatch->q_out_cb<7>().set(FUNC(tomcat_state::txbuff_w));

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0);

	WATCHDOG_TIMER(config, "watchdog");

	MCFG_DEVICE_ADD("m48t02", M48T02, 0)

	MCFG_VECTOR_ADD("vector")
	MCFG_SCREEN_ADD("screen", VECTOR)
	MCFG_SCREEN_REFRESH_RATE(40)
	//MCFG_SCREEN_REFRESH_RATE((double)XTAL(12'000'000) / 16 / 16 / 16 / 12  / 5 )
	MCFG_SCREEN_SIZE(400, 300)
	MCFG_SCREEN_VISIBLE_AREA(0, 280, 0, 250)
	MCFG_SCREEN_UPDATE_DEVICE("vector", vector_device, screen_update)

	MCFG_DEVICE_ADD("avg", AVG_TOMCAT, 0)
	MCFG_AVGDVG_VECTOR("vector")

	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();
	MCFG_DEVICE_ADD("pokey1", POKEY, XTAL(14'318'181) / 8)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "lspeaker", 0.20)

	MCFG_DEVICE_ADD("pokey2", POKEY, XTAL(14'318'181) / 8)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "rspeaker", 0.20)

	MCFG_DEVICE_ADD("tms", TMS5220, 325000)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "lspeaker", 0.50)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "rspeaker", 0.50)

	YM2151(config, "ymsnd", XTAL(14'318'181)/4).add_route(0, "lspeaker", 0.60).add_route(1, "rspeaker", 0.60);
MACHINE_CONFIG_END

ROM_START( tomcat )
	ROM_REGION( 0x10000, "maincpu", 0)
	ROM_LOAD16_BYTE( "rom1k.bin", 0x00001, 0x8000, CRC(5535a1ff) SHA1(b9807c749a8e6b5ddec3ff494130abda09f0baab) )
	ROM_LOAD16_BYTE( "rom2k.bin", 0x00000, 0x8000, CRC(021a01d2) SHA1(01d99aab54ad57a664e8aaa91296bb879fc6e422) )

	ROM_REGION( 0x100, "user1", 0 )
	ROM_LOAD( "136021-105.1l",   0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6) ) /* AVG PROM */
ROM_END

GAME( 1985, tomcat, 0,        tomcat, tomcat, tomcat_state, empty_init, ROT0, "Atari", "TomCat (prototype)", MACHINE_SUPPORTS_SAVE )
