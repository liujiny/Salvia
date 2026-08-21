// ---------------------------------------------------------------------------
//	FM Sound Generator
//	Copyright (C) cisc 1998, 2001.
// ---------------------------------------------------------------------------

#ifndef FM_GEN_H
#define FM_GEN_H

#include <stdint.h>
#include "common.h"

// ---------------------------------------------------------------------------
//	�����Σ�
//	��Ū�ơ��֥�Υ�����

#define FM_LFOBITS		8
#define FM_TLBITS		7

// ---------------------------------------------------------------------------

#define FM_TLENTS		(1 << FM_TLBITS)
#define FM_LFOENTS		(1 << FM_LFOBITS)
#define FM_TLPOS		(FM_TLENTS/4)

//	�������Ȥ����٤� 2^(1/256)
#define FM_CLENTS		(0x1000 * 2)	// sin + TL + LFO

// ---------------------------------------------------------------------------

namespace FM
{	
	//	Types ----------------------------------------------------------------
	typedef int32_t ISample;

	enum	OpType { TYPE_N = 0, TYPE_M = 1 };
	enum	EGPhase { NEXT, ATTACK, DECAY, SUSTAIN, RELEASE, OFF };

	void StoreSample(ISample& dest, int data);

	class Chip;

	//	Operator -------------------------------------------------------------
	class Operator
	{
	public:
		Operator();
		void	SetChip(Chip* chip) { chip_ = chip; }

		ISample	Calc(ISample in);
		ISample	CalcL(ISample in);
		ISample CalcFB(uint32_t fb);
		ISample CalcFBL(uint32_t fb);
		ISample CalcN(uint32_t noise);
		void	Prepare();
		void	KeyOn();
		void	KeyOff();
		void	Reset();
		int	IsOn();

		void	SetDT(uint32_t dt);
		void	SetDT2(uint32_t dt2);
		void	SetMULTI(uint32_t multi);
		void	SetTL(uint32_t tl, bool csm);
		void	SetKS(uint32_t ks);
		void	SetAR(uint32_t ar);
		void	SetDR(uint32_t dr);
		void	SetSR(uint32_t sr);
		void	SetRR(uint32_t rr);
		void	SetSL(uint32_t sl);
		void	SetSSGEC(uint32_t ssgec);
		void	SetFNum(uint32_t fnum);
		void	SetDPBN(uint32_t dp, uint32_t bn);
		void	SetMode(bool modulator);
		void	SetAMON(bool on);
		void	SetMS(uint32_t ms);
		void	Mute(bool);
		
		int	Out() { return out_; }

		int StateAction(StateMem *sm, int load, int data_only, const char *sname);
	
	private:
		Chip*	chip_;
		ISample	out_, out2_;

	//	Phase Generator ------------------------------------------------------
		uint32_t	PGCalc();
		uint32_t	PGCalcL();

		uint32_t	dp_;		// ��P
		uint32_t	detune_;		// Detune
		uint32_t	detune2_;	// DT2
		uint32_t	multiple_;	// Multiple
		uint32_t	pg_count_;	// Phase ������
		uint32_t	pg_diff_;	// Phase ��ʬ��
		int32_t		pg_diff_lfo_;	// Phase ��ʬ�� >> x

	//	Envelop Generator ---------------------------------------------------
		void	EGCalc();
		void	EGStep();
		void	ShiftPhase(EGPhase nextphase);
		void	SSGShiftPhase(int mode);
		void	SetEGRate(uint32_t);
		void	EGUpdate();
		ISample LogToLin(uint32_t a);

		OpType		type_;		// OP �μ��� (M, N...)
		uint32_t	bn_;		// Block/Note
		int		eg_level_;	// EG �ν�����
		int		eg_level_on_next_phase_;	// ���� eg_phase_ �˰ܤ���
		int		eg_count_;		// EG �μ����ѰܤޤǤλ���
		int		eg_count_diff_;	// eg_count_ �κ�ʬ
		int		eg_out_;		// EG+TL ���碌��������
		int		tl_out_;		// TL ʬ�ν�����
//		int		pm_depth_;		// PM depth
//		int		am_depth_;		// AM depth
		int		eg_rate_;
		int		eg_curve_count_;
		int		ssg_offset_;
		int		ssg_vector_;
		int		ssg_phase_;


		uint32_t	key_scale_rate_;		// key scale rate
		EGPhase	eg_phase_;
		uint32_t*	ams_;
		uint32_t	ms_;
		
		uint32_t	tl_;			// Total Level	 (0-127)
		uint32_t	tl_latch_;		// Total Level Latch (for CSM mode)
		uint32_t	ar_;			// Attack Rate   (0-63)
		uint32_t	dr_;			// Decay Rate    (0-63)
		uint32_t	sr_;			// Sustain Rate  (0-63)
		uint32_t	sl_;			// Sustain Level (0-127)
		uint32_t	rr_;			// Release Rate  (0-63)
		uint32_t	ks_;			// Keyscale      (0-3)
		uint32_t	ssg_type_;	// SSG-Type Envelop Control

		bool	keyon_;
		bool	amon_;		// enable Amplitude Modulation
		bool	param_changed_;	// �ѥ�᡼�����������줿
		bool	mute_;
		
	//	Tables ---------------------------------------------------------------
		static uint32_t rate_table[16];
		static uint32_t multable[4][16];

		static const uint8_t notetable[128];
		static const int8_t dttable[256];
		static const int8_t decaytable1[64][8];
		static const int decaytable2[16];
		static const int8_t attacktable[64][8];
		static const int ssgenvtable[8][2][3][2];

		static uint32_t	sinetable[1024];
		static int32_t cltable[FM_CLENTS];

		static bool tablehasmade;
		static void MakeTable();

	//	friends --------------------------------------------------------------
		friend class Channel4;
	};
	
	//	4-op Channel ---------------------------------------------------------
	class Channel4
	{
	public:
		Channel4();
		void SetChip(Chip* chip);
		void SetType(OpType type);
		
		ISample Calc();
		ISample CalcL();
		ISample CalcN(uint32_t noise);
		ISample CalcLN(uint32_t noise);
		void SetFNum(uint32_t fnum);
		void SetFB(uint32_t fb);
		void SetKCKF(uint32_t kc, uint32_t kf);
		void SetAlgorithm(uint32_t algo);
		int Prepare();
		void KeyControl(uint32_t key);
		void Reset();
		void SetMS(uint32_t ms);
		void Mute(bool);

		int StateAction(StateMem *mem, int load, int data_only, const char *sname);
	
	private:
		static const uint8_t fbtable[8];
		uint32_t fb;
		int	 buf[4];
		int*	 in[3];			// �� OP �����ϥݥ���
		int*	 out[3];		// �� OP �ν��ϥݥ���
		int*	 pms;
		
		Chip*	 chip_;
		int	 algo_;

		static void MakeTable();

		static bool tablehasmade;
		static int 	kftable[64];


	public:
		Operator op[4];
	};

	//	Chip resource
	class Chip
	{
	public:
		Chip();
		void	SetRatio(uint32_t ratio);
		void	SetAML(uint32_t l);
		void	SetPML(uint32_t l);
		void	SetPMV(int pmv) { pmv_ = pmv; }

		uint32_t	GetMulValue(uint32_t dt2, uint32_t mul) { return multable_[dt2][mul]; }
		uint32_t	GetAML() { return aml_; }
		uint32_t	GetPML() { return pml_; }
		int		GetPMV() { return pmv_; }
		uint32_t	GetRatio() { return ratio_; }

		int StateAction(StateMem *sm, int load, int data_pnly);

	private:
		void	MakeTable();

		uint32_t	ratio_;
		uint32_t	aml_;
		uint32_t	pml_;
		int		pmv_;
		OpType	optype_;
		uint32_t	multable_[4][16];
	};
}

#endif // FM_GEN_H
