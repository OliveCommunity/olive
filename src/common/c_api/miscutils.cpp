/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "common/miscutils.h"

#include "../src/decibel.h"
#include "../src/lerp.h"

int oakcommon_decibel_from_linear(double linear, double *out_db)
{
	if (!out_db) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_db = olive::Decibel::from_linear(linear);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_decibel_to_linear(double db, double *out_linear)
{
	if (!out_linear) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_linear = olive::Decibel::to_linear(db);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_decibel_from_logarithmic(double logarithmic, double *out_db)
{
	if (!out_db) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_db = olive::Decibel::from_logarithmic(logarithmic);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_decibel_to_logarithmic(double db, double *out_logarithmic)
{
	if (!out_logarithmic) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_logarithmic = olive::Decibel::to_logarithmic(db);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_decibel_linear_to_logarithmic(double linear,
											double *out_logarithmic)
{
	if (!out_logarithmic) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_logarithmic = olive::Decibel::linear_to_logarithmic(linear);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_decibel_logarithmic_to_linear(double logarithmic,
											double *out_linear)
{
	if (!out_linear) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_linear = olive::Decibel::logarithmic_to_linear(logarithmic);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_lerp(double a, double b, double t, double *out_value)
{
	if (!out_value) {
		return OAKCOMMON_E_INVALID;
	}
	try {
		*out_value = lerp(a, b, t);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
