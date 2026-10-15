/* Test-only arithmetic transcription, never linked into main.
 * Source: EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.c
 * C_Terminal_Data (line 2465), C_exp (line 3866); sreal = double.
 * Changes: names/types, Tmin expanded to 1e-6, comments removed.
 * Caller supplies finite input. Original soft protection above 500 retained.
 */
#include "check_s04.h"

static double reference_terminal_data(double Data,double Terminal,double Doorsill)
{
	double asd;

	if (Doorsill>Terminal + 1e-6*1e-6)
	{
		if(Data<Doorsill)
		{
			asd		= 1/(Terminal-Doorsill);

			Data	= Doorsill+(Data-Doorsill)/(1+(Data-Doorsill)*asd);
		}
		return Data;
	}
	else if(Doorsill<Terminal-1e-6*1e-6)
	{
		if(Data>Doorsill)
		{
			asd	= 1/(Terminal-Doorsill);

			Data	= Doorsill + (Data-Doorsill)/(1+(Data-Doorsill)*asd);
		}
		return Data;
	}
	else
	{
		return Data;
	}
}

double reference_exp(double a)					
{
	int i,n,k;
	double y,x,y0;
	int		exp_n[12] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
	double  exp_y[15] = {   2.7182818284590452353602874713527,
							7.389056098930650227230427460575, 
							54.598150033144239078110261202861,
							2980.9579870417282747435920994529,
							8886110.5205078726367630237407815,
							78962960182680.695160978022635108,
							6235149080811616882909238708.9285,
							3.8877084059945950922226736883575e+55,
							1.5114276650041035425200896657073e+111,
							2.2844135865397566403787515171224e+222,
							2.2255409284924676045795375313951,
							1.8221188003905089748753676681629,
							1.4918246976412703178248529528372,
							1.2214027581601698339210719946397
							};	

	if (a < 0)
	{
		n = 1;
		a = -a;
	}
	else
	{
		n = 0;
	}

	{
		if (a > 500)
		{
			a = reference_terminal_data(a, 700.0, 500.0);;
		}
		k = (int)a;
		a = a-k;
	}
	 
	y = 1;
	for (i = 9; i >=0; i--)
	{
		if (exp_n[i] <= k)
		{
			k = k - exp_n[i];
			y = y * exp_y[i];
		}
	}

	if (a > 0.8)
	{
		a = a - 0.8;
		y = y * exp_y[10];
	}
	else if (a > 0.6)
	{
		a = a - 0.6;
		y = y * exp_y[11];
	}
	else if (a > 0.4)
	{
		a = a - 0.4;
		y = y * exp_y[12];
	}
	else if (a > 0.2)
	{
		a = a - 0.2;
		y = y * exp_y[13];
	}

	{
		x = 1.0;		y0 = 1.0;
		x = x*a;		y0 += x;
		x = x*a;		y0 += x*0.5;
		x = x*a;		y0 += x*0.16666666666666666666666666666667;
		x = x*a;		y0 += x*0.04166666666666666666666666666667;
		x = x*a;		y0 += x*0.00833333333333333333333333333333;
		x = x*a;		y0 += x*0.00138888888888888888888888888889;
		x = x*a;		y0 += x*0.0001984126984126984126984126984127;
		x = x*a;		y0 += x*0.000024801587301587301587301587301587;
		x = x*a;		y0 += x*0.0000027557319223985890652557319223986;
		x = x*a;		y0 += x*0.00000027557319223985890652557319223986;

	}
	y = y0*y;

	if (n == 1)
	{
		y = 1 / y;
	}

	return y;
}
