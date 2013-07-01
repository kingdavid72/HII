#include <iomanip>
#include <random>
#include "utility.hpp"
#include "ligand.hpp"

void frame::output(boost::filesystem::ofstream& ofs) const
{
	ofs << "BRANCH"    << setw(4) << rotorXsrn << setw(4) << rotorYsrn << '\n';
}

ligand::ligand(const path& p) : num_active_torsions(0)
{
	// Initialize necessary variables for constructing a ligand.
	lines.reserve(200); // A ligand typically consists of <= 200 lines.
	frames.reserve(30); // A ligand typically consists of <= 30 frames.
	frames.push_back(frame(0, 0, 1, 0, 0, 0)); // ROOT is also treated as a frame. The parent and rotorX of ROOT frame are dummy.
	heavy_atoms.reserve(100); // A ligand typically consists of <= 100 heavy atoms.
	hydrogens.reserve(50); // A ligand typically consists of <= 50 hydrogens.

	// Initialize helper variables for parsing.
	vector<vector<size_t>> bonds; ///< Covalent bonds.
	bonds.reserve(100); // A ligand typically consists of <= 100 heavy atoms.
	size_t current = 0; // Index of current frame, initialized to ROOT frame.
	frame* f = &frames.front(); // Pointer to the current frame.
	f->rotorYidx = 0; // Assume the rotorY of ROOT frame is the first atom.
	string line;

	// Parse the ligand line by line.
	for (boost::filesystem::ifstream ifs(p); getline(ifs, line);)
	{
		const string record = line.substr(0, 6);
		if (record == "ATOM  " || record == "HETATM")
		{
			// Whenever an ATOM/HETATM line shows up, the current frame must be the last one.
			assert(current == frames.size() - 1);
			assert(f == &frames.back());

			// This line will be dumped to the output ligand file.
			lines.push_back(line);

			// Parse the line.
			atom a(line);

			// Skip unsupported atom types.
			if (a.ad_unsupported()) continue;

			if (a.is_hydrogen()) // Current atom is a hydrogen.
			{
				// For a polar hydrogen, the bonded hetero atom must be a hydrogen bond donor.
				if (a.is_polar_hydrogen())
				{
					for (size_t i = heavy_atoms.size(); i > f->habegin;)
					{
						atom& b = heavy_atoms[--i];
						if (!b.is_hetero()) continue; // Only a hetero atom can be a hydrogen bond donor.
						if (a.has_covalent_bond(b))
						{
							b.donorize();
							break;
						}
					}
				}

				// Save the hydrogen.
				hydrogens.push_back(a);
			}
			else // Current atom is a heavy atom.
			{
				// Find bonds between the current atom and the other atoms of the same frame.
				assert(bonds.size() == heavy_atoms.size());
				bonds.push_back(vector<size_t>());
				bonds.back().reserve(4); // An atom typically consists of <= 4 bonds.
				for (size_t i = heavy_atoms.size(); i > f->habegin;)
				{
					atom& b = heavy_atoms[--i];
					if (a.has_covalent_bond(b))
					{
						bonds[heavy_atoms.size()].push_back(i);
						bonds[i].push_back(heavy_atoms.size());

						// If carbon atom b is bonded to hetero atom a, b is no longer a hydrophobic atom.
						if (a.is_hetero() && !b.is_hetero())
						{
							b.dehydrophobicize();
						}
						// If carbon atom a is bonded to hetero atom b, a is no longer a hydrophobic atom.
						else if (!a.is_hetero() && b.is_hetero())
						{
							a.dehydrophobicize();
						}
					}
				}

				// Set rotorYidx if the serial number of current atom is rotorYsrn.
				if (current && (a.serial == f->rotorYsrn)) // current > 0, i.e. BRANCH frame.
				{
					f->rotorYidx = heavy_atoms.size();
				}

				// Save the heavy atom.
				heavy_atoms.push_back(a);
			}
		}
		else if (record == "BRANCH")
		{
			// This line will be dumped to the output ligand file.
			lines.push_back(line);

			// Parse "BRANCH   X   Y". X and Y are right-justified and 4 characters wide.
			const size_t rotorXsrn = stoul(line.substr( 6, 4));
			const size_t rotorYsrn = stoul(line.substr(10, 4));

			// Find the corresponding heavy atom with x as its atom serial number in the current frame.
			for (size_t i = f->habegin; true; ++i)
			{
				if (heavy_atoms[i].serial == rotorXsrn)
				{
					// Insert a new frame whose parent is the current frame.
					frames.push_back(frame(current, rotorXsrn, rotorYsrn, i, heavy_atoms.size(), hydrogens.size()));
					break;
				}
			}

			// The current frame has the newly inserted BRANCH frame as one of its branches.
			// It is unsafe to use f in place of frames[current] because frames could reserve a new memory block after calling push_back().
			frames[current].branches.push_back(frames.size() - 1);

			// Now the current frame is the newly inserted BRANCH frame.
			current = frames.size() - 1;

			// Update the pointer to the current frame.
			f = &frames[current];

			// The ending index of atoms of previous frame is the starting index of atoms of current frame.
			frames[current - 1].haend = f->habegin;
			frames[current - 1].hyend = f->hybegin;
		}
		else if (record == "ENDBRA")
		{
			// This line will be dumped to the output ligand file.
			lines.push_back(line);

			// A frame may be empty, e.g. "BRANCH   4   9" is immediately followed by "ENDBRANCH   4   9".
			// This emptiness is likely to be caused by invalid input structure, especially when all the atoms are located in the same plane.
			if (f->habegin == heavy_atoms.size()) throw domain_error("Error parsing " + p.filename().string() + ": an empty BRANCH has been detected, indicating the input ligand structure is probably invalid.");

			// If the current frame consists of rotor Y and a few hydrogens only, e.g. -OH and -NH2,
			// the torsion of this frame will have no effect on scoring and is thus redundant.
			if ((current == frames.size() - 1) && (f->habegin + 1 == heavy_atoms.size()))
			{
				f->active = false;
			}
			else
			{
				++num_active_torsions;
			}

			// Set up bonds between rotorX and rotorY.
			bonds[f->rotorYidx].push_back(f->rotorXidx);
			bonds[f->rotorXidx].push_back(f->rotorYidx);

			// Dehydrophobicize rotorX and rotorY if necessary.
			atom& rotorY = heavy_atoms[f->rotorYidx];
			atom& rotorX = heavy_atoms[f->rotorXidx];
			if ((rotorY.is_hetero()) && (!rotorX.is_hetero())) rotorX.dehydrophobicize();
			if ((rotorX.is_hetero()) && (!rotorY.is_hetero())) rotorY.dehydrophobicize();

			// Calculate parent_rotorY_to_current_rotorY and parent_rotorX_to_current_rotorY.
			const frame& p = frames[f->parent];
			f->parent_rotorY_to_current_rotorY =  rotorY.coord - heavy_atoms[p.rotorYidx].coord;
			f->parent_rotorX_to_current_rotorY = normalize(rotorY.coord - rotorX.coord);

			// Now the parent of the following frame is the parent of current frame.
			current = f->parent;

			// Update the pointer to the current frame.
			f = &frames[current];
		}
		else if (record == "ROOT" || record == "ENDROO" || record == "TORSDO")
		{
			// This line will be dumped to the output ligand file.
			lines.push_back(line);
		}
	}
	assert(current == 0); // current should remain its original value if "BRANCH" and "ENDBRANCH" properly match each other.
	assert(f == &frames.front()); // The frame pointer should remain its original value if "BRANCH" and "ENDBRANCH" properly match each other.

	// Determine num_heavy_atoms, num_hydrogens, and num_heavy_atoms_inverse.
	num_heavy_atoms = heavy_atoms.size();
	num_hydrogens = hydrogens.size();
	frames.back().haend = num_heavy_atoms;
	frames.back().hyend = num_hydrogens;
	num_heavy_atoms_inverse = 1.0f / num_heavy_atoms;

	// Determine num_frames, num_torsions, flexibility_penalty_factor.
	num_frames = frames.size();
	assert(num_frames >= 1);
	num_torsions = num_frames - 1;
	assert(num_torsions + 1 == num_frames);
	assert(num_torsions >= num_active_torsions);
	assert(num_heavy_atoms + num_hydrogens + (num_torsions << 1) + 3 == lines.size()); // ATOM/HETATM lines + BRANCH/ENDBRANCH lines + ROOT/ENDROOT/TORSDOF lines == lines.size()

	// Update heavy_atoms[].coord and hydrogens[].coord relative to frame origin.
	for (size_t k = 0; k < num_frames; ++k)
	{
		const frame& f = frames[k];
		const array<float, 3> origin = heavy_atoms[f.rotorYidx].coord;
		for (size_t i = f.habegin; i < f.haend; ++i)
		{
			heavy_atoms[i].coord -= origin;
		}
		for (size_t i = f.hybegin; i < f.hyend; ++i)
		{
			hydrogens[i].coord -= origin;
		}
	}

	// Find intra-ligand interacting pairs that are not 1-4.
	interacting_pairs.reserve(num_heavy_atoms * num_heavy_atoms);
	vector<size_t> neighbors;
	neighbors.reserve(10); // An atom typically consists of <= 10 neighbors.
	for (size_t k1 = 0; k1 < num_frames; ++k1)
	{
		const frame& f1 = frames[k1];
		for (size_t i = f1.habegin; i < f1.haend; ++i)
		{
			// Find neighbor atoms within 3 consecutive covalent bonds.
			const vector<size_t>& i0_bonds = bonds[i];
			const size_t num_i0_bonds = i0_bonds.size();
			for (size_t i0 = 0; i0 < num_i0_bonds; ++i0)
			{
				const size_t b1 = i0_bonds[i0];
				if (find(neighbors.begin(), neighbors.end(), b1) == neighbors.end())
				{
					neighbors.push_back(b1);
				}
				const vector<size_t>& i1_bonds = bonds[b1];
				const size_t num_i1_bonds = i1_bonds.size();
				for (size_t i1 = 0; i1 < num_i1_bonds; ++i1)
				{
					const size_t b2 = i1_bonds[i1];
					if (find(neighbors.begin(), neighbors.end(), b2) == neighbors.end())
					{
						neighbors.push_back(b2);
					}
					const vector<size_t>& i2_bonds = bonds[b2];
					const size_t num_i2_bonds = i2_bonds.size();
					for (size_t i2 = 0; i2 < num_i2_bonds; ++i2)
					{
						const size_t b3 = i2_bonds[i2];
						if (find(neighbors.begin(), neighbors.end(), b3) == neighbors.end())
						{
							neighbors.push_back(b3);
						}
					}
				}
			}

			// Determine if interacting pairs can be possibly formed.
			for (size_t k2 = k1 + 1; k2 < num_frames; ++k2)
			{
				const frame& f2 = frames[k2];
				const frame& f3 = frames[f2.parent];
				for (size_t j = f2.habegin; j < f2.haend; ++j)
				{
					if (k1 == f2.parent && (i == f2.rotorXidx || j == f2.rotorYidx)) continue;
					if (k1 > 0 && f1.parent == f2.parent && i == f1.rotorYidx && j == f2.rotorYidx) continue;
					if (f2.parent > 0 && k1 == f3.parent && i == f3.rotorXidx && j == f2.rotorYidx) continue;
					if (find(neighbors.cbegin(), neighbors.cend(), j) != neighbors.cend()) continue;
					const size_t p_offset = scoring_function::nr * mp(heavy_atoms[i].xs, heavy_atoms[j].xs);
					interacting_pairs.push_back(interacting_pair(i, j, p_offset));
				}
			}

			// Clear the current neighbor set for the next atom.
			neighbors.clear();
		}
	}
}

bool ligand::evaluate(const vector<float>& x, const scoring_function& sf, const receptor& rec, const float e_upper_bound, float& e, vector<float>& g) const
{
	// Initialize frame-wide conformational variables.
	vector<array<float, 3>> o(num_frames); ///< Origin coordinate, which is rotorY.
	vector<array<float, 3>> a(num_frames); ///< Vector pointing from rotor Y to rotor X.
	vector<array<float, 4>> q(num_frames); ///< Orientation in the form of quaternion.
	vector<array<float, 3>> gf(num_frames, zero3); ///< Aggregated derivatives of heavy atoms.
	vector<array<float, 3>> gt(num_frames, zero3); /// Torque of the force.

	// Initialize atom-wide conformational variables.
	vector<array<float, 3>> c(num_heavy_atoms); ///< Heavy atom coordinates.
	vector<array<float, 3>> d(num_heavy_atoms); ///< Heavy atom derivatives.

	// Apply position and orientation to ROOT frame.
	const frame& root = frames.front();
	o.front()[0] = x[0];
	o.front()[1] = x[1];
	o.front()[2] = x[2];
	q.front()[0] = x[3];
	q.front()[1] = x[4];
	q.front()[2] = x[5];
	q.front()[3] = x[6];

	// Apply torsions to frames.
	for (size_t k = 0, t = 0; k < num_frames; ++k)
	{
		const frame& f = frames[k];
		const array<float, 9> m = qtn4_to_mat3(q[k]);
		for (size_t i = f.habegin; i < f.haend; ++i)
		{
			c[i] = o[k] + m * heavy_atoms[i].coord;
		}
		for (const size_t i : f.branches)
		{
			const frame& b = frames[i];
			o[i] = o[k] + m * b.parent_rotorY_to_current_rotorY;

			// If the current BRANCH frame does not have an active torsion, skip it.
			if (!b.active)
			{
				assert(b.habegin + 1 == b.haend);
				assert(b.habegin == b.rotorYidx);
//				c[b.rotorYidx] = o[i];
				continue;
			}
			assert(normalized(b.parent_rotorX_to_current_rotorY));
			a[i] = m * b.parent_rotorX_to_current_rotorY;
			assert(normalized(a[i]));
			q[i] = vec4_to_qtn4(a[i], x[7 + t++]) * q[k];
			assert(normalized(q[i]));
		}
	}

	// Check steric clash between atoms of different frames except for (rotorX, rotorY) pair.
	//for (size_t k1 = num_frames - 1; k1 > 0; --k1)
	//{
	//	const frame& f1 = frames[k1];
	//	for (size_t i1 = f1.habegin; i1 < f1.haend; ++i1)
	//	{
	//		for (size_t k2 = 0; k2 < k1; ++k2)
	//		{
	//			const frame& f2 = frames[k2];
	//			for (size_t i2 = f2.habegin; i2 < f2.haend; ++i2)
	//			{
	//				if ((distance_sqr(c[i1], c[i2]) < sqr(heavy_atoms[i1].covalent_radius() + heavy_atoms[i2].covalent_radius())) && (!((k2 == f1.parent) && (i1 == f1.rotorYidx) && (i2 == f1.rotorXidx))))
	//					return false;
	//			}
	//		}
	//	}
	//}

	e = 0;
	for (size_t i = 0; i < num_heavy_atoms; ++i)
	{
		if (!rec.within(c[i]))
		{
			e += 10;
			d[i][0] = 0;
			d[i][1] = 0;
			d[i][2] = 0;
			continue;
		}

		// Retrieve the grid map in need.
		const vector<float>& map = rec.maps[heavy_atoms[i].xs];
		assert(map.size());

		// Find the index of the current coordinates.
		const array<size_t, 3> index = rec.coordinate_to_index(c[i]);

		// Calculate the offsets to grid map and lookup the values.
		const size_t o000 = rec.num_probes[0] * (rec.num_probes[1] * index[2] + index[1]) + index[0];
		const size_t o100 = o000 + 1;
		const size_t o010 = o000 + rec.num_probes[0];
		const size_t o001 = o000 + rec.num_probes[0] * rec.num_probes[1];
		const float e000 = map[o000];
		const float e100 = map[o100];
		const float e010 = map[o010];
		const float e001 = map[o001];
		d[i][0] = (e100 - e000) * rec.granularity_inverse;
		d[i][1] = (e010 - e000) * rec.granularity_inverse;
		d[i][2] = (e001 - e000) * rec.granularity_inverse;

		e += e000; // Aggregate the energy.
	}

	// Calculate intra-ligand free energy.
	const size_t num_interacting_pairs = interacting_pairs.size();
	for (size_t i = 0; i < num_interacting_pairs; ++i)
	{
		const interacting_pair& p = interacting_pairs[i];
		const array<float, 3> r = c[p.i2] - c[p.i1];
		const float r2 = norm_sqr(r);
		if (r2 < scoring_function::cutoff_sqr)
		{
			const size_t o = p.p_offset + static_cast<size_t>(sf.ns * r2);
			e += sf.e[o];
			const array<float, 3> derivative = sf.d[o] * r;
			d[p.i1] -= derivative;
			d[p.i2] += derivative;
		}
	}

	// If the free energy is no better than the upper bound, refuse this conformation.
	if (e >= e_upper_bound) return false;

	// Calculate and aggregate the force and torque of BRANCH frames to their parent frame.
	for (size_t k = num_frames, t = num_active_torsions; --k;)
	{
		const frame&  f = frames[k];

		for (size_t i = f.habegin; i < f.haend; ++i)
		{
			// The derivatives with respect to the position, orientation, and torsions
			// would be the negative total force acting on the ligand,
			// the negative total torque, and the negative torque projections, respectively,
			// where the projections refer to the torque applied to the branch moved by the torsion,
			// projected on its rotation axis.
			gf[k]  += d[i];
			gt[k] += (c[i] - o[k]) * d[i];
		}

		// Aggregate the force and torque of current frame to its parent frame.
		gf[f.parent]  += gf[k];
		gt[f.parent] += gt[k] + (o[k] - o[f.parent]) * gf[k];

		// If the current BRANCH frame does not have an active torsion, skip it.
		if (!f.active) continue;

		// Save the torsion.
		g[6 + (--t)] = gt[k][0] * a[k][0] + gt[k][1] * a[k][1] + gt[k][2] * a[k][2]; // dot product
	}

	// Calculate and aggregate the force and torque of ROOT frame.
	for (size_t i = root.habegin; i < root.haend; ++i)
	{
		gf.front()  += d[i];
		gt.front() += (c[i] - o.front()) * d[i];
	}

	// Save the aggregated force and torque to g.
	g[0] = gf.front()[0];
	g[1] = gf.front()[1];
	g[2] = gf.front()[2];
	g[3] = gt.front()[0];
	g[4] = gt.front()[1];
	g[5] = gt.front()[2];

	return true;
}

result ligand::compose_result(const float e, const vector<float>& x) const
{
	vector<array<float, 3>> o(num_frames);
	vector<array<float, 4>> q(num_frames);
	vector<array<float, 3>> heavy_atoms(num_heavy_atoms);
	vector<array<float, 3>> hydrogens(num_hydrogens);

	o.front()[0] = x[0];
	o.front()[1] = x[1];
	o.front()[2] = x[2];
	q.front()[0] = x[3];
	q.front()[1] = x[4];
	q.front()[2] = x[5];
	q.front()[3] = x[6];

	// Calculate the coordinates of both heavy atoms and hydrogens of BRANCH frames.
	for (size_t k = 0, t = 0; k < num_frames; ++k)
	{
		const frame& f = frames[k];
		const array<float, 9> m = qtn4_to_mat3(q[k]);
		for (size_t i = f.habegin; i < f.haend; ++i)
		{
			heavy_atoms[i] = o[k] + m * this->heavy_atoms[i].coord;
		}
		for (size_t i = f.hybegin; i < f.hyend; ++i)
		{
			hydrogens[i]   = o[k] + m * this->hydrogens[i].coord;
		}
		for (const size_t i : f.branches)
		{
			const frame& b = frames[i];
			o[i] = o[k] + m * b.parent_rotorY_to_current_rotorY;
			q[i] = vec4_to_qtn4(m * b.parent_rotorX_to_current_rotorY, f.active ? x[7 + t++] : 0.0f) * q[k];
		}
	}

	return result(e, static_cast<vector<array<float, 3>>&&>(heavy_atoms), static_cast<vector<array<float, 3>>&&>(hydrogens));
}

int ligand::bfgs(result& r, const scoring_function& sf, const receptor& rec, const size_t seed, const size_t num_generations) const
{
	// Define constants.
	const size_t num_alphas = 5; // Number of alpha values for determining step size in BFGS
	const size_t num_variables = 6 + num_active_torsions; // Number of variables to optimize.
	const float e_upper_bound = 40.0f * num_heavy_atoms; // A conformation will be droped if its free energy is not better than e_upper_bound.

	// Declare variable.
	vector<float> x0(7 + num_active_torsions), x1(7 + num_active_torsions), x2(7 + num_active_torsions);
	vector<float> g0(6 + num_active_torsions), g1(6 + num_active_torsions), g2(6 + num_active_torsions);
	vector<float> p(6 + num_active_torsions), y(6 + num_active_torsions), mhy(6 + num_active_torsions);
	vector<float> h(num_variables*(num_variables+1)>>1); // Symmetric triangular Hessian matrix.
	float e0, e1, e2, alpha, pg1, pg2, yhy, yp, ryp, pco;
	size_t g, i, j;
	mt19937_64 rng(seed);
	uniform_real_distribution<float> uniform_11(-1.0f, 1.0f);

	// Randomize conformation x0.
	x0[0] = rec.center[0] + uniform_11(rng) * rec.size[0];
	x0[1] = rec.center[1] + uniform_11(rng) * rec.size[1];
	x0[2] = rec.center[2] + uniform_11(rng) * rec.size[2];
	const array<float, 4> rnorientation = {uniform_11(rng), uniform_11(rng), uniform_11(rng), uniform_11(rng)};
	const array<float, 4> x0orientation = normalize(rnorientation);
	assert(normalized(x0orientation));
	x0[3] = x0orientation[0];
	x0[4] = x0orientation[1];
	x0[5] = x0orientation[2];
	x0[6] = x0orientation[3];
	for (i = 0; i < num_active_torsions; ++i)
	{
		x0[7 + i] = uniform_11(rng);
	}
	evaluate(x0, sf, rec, e_upper_bound, e0, g0);
	r = compose_result(e0, x0);

	for (g = 0; g < num_generations; ++g)
	{
		// Make a copy, so the previous conformation is retained.
		x1 = x0;
		x1[0] += uniform_11(rng);
		x1[1] += uniform_11(rng);
		x1[2] += uniform_11(rng);
		evaluate(x1, sf, rec, e_upper_bound, e1, g1);

		// Initialize the inverse Hessian matrix to identity matrix.
		// An easier option that works fine in practice is to use a scalar multiple of the identity matrix,
		// where the scaling factor is chosen to be in the range of the eigenvalues of the true Hessian.
		// See N&R for a recipe to find this initializer.
		fill(h.begin(), h.end(), 0.0f);
		for (i = 0; i < num_variables; ++i)
			h[mr(i, i)] = 1.0f;

		// Given the mutated conformation c1, use BFGS to find a local minimum.
		// The conformation of the local minimum is saved to c2, and its derivative is saved to g2.
		// http://en.wikipedia.org/wiki/BFGS_method
		// http://en.wikipedia.org/wiki/Quasi-Newton_method
		// The loop breaks when an appropriate alpha cannot be found.
		while (true)
		{
			// Calculate p = -h*g, where p is for descent direction, h for Hessian, and g for gradient.
			for (i = 0; i < num_variables; ++i)
			{
				float sum = 0.0f;
				for (j = 0; j < num_variables; ++j)
					sum += h[mp(i, j)] * g1[j];
				p[i] = -sum;
			}

			// Calculate pg = p*g = -h*g^2 < 0
			pg1 = 0;
			for (i = 0; i < num_variables; ++i)
				pg1 += p[i] * g1[i];

			// Perform a line search to find an appropriate alpha.
			// Try different alpha values for num_alphas times.
			// alpha starts with 1, and shrinks to alpha_factor of itself iteration by iteration.
			alpha = 1.0;
			for (j = 0; j < num_alphas; ++j)
			{
				// Calculate c2 = c1 + ap.
				x2[0] = x1[0] + alpha * p[0];
				x2[1] = x1[1] + alpha * p[1];
				x2[2] = x1[2] + alpha * p[2];
				const array<float, 4> x1orientation = { x1[3], x1[4], x1[5], x1[6] };
				assert(normalized(x1orientation));
				const array<float, 4> x2orientation = vec3_to_qtn4(alpha * make_array(p[3], p[4], p[5])) * x1orientation;
				assert(normalized(x2orientation));
				x2[3] = x2orientation[0];
				x2[4] = x2orientation[1];
				x2[5] = x2orientation[2];
				x2[6] = x2orientation[3];
				for (i = 0; i < num_active_torsions; ++i)
				{
					x2[7 + i] = x1[7 + i] + alpha * p[6 + i];
				}

				// Evaluate c2, subject to Wolfe conditions http://en.wikipedia.org/wiki/Wolfe_conditions
				// 1) Armijo rule ensures that the step length alpha decreases f sufficiently.
				// 2) The curvature condition ensures that the slope has been reduced sufficiently.
				if (evaluate(x2, sf, rec, e1 + 0.0001f * alpha * pg1, e2, g2))
				{
					pg2 = 0;
					for (i = 0; i < num_variables; ++i)
						pg2 += p[i] * g2[i];
					if (pg2 >= 0.9f * pg1)
						break; // An appropriate alpha is found.
				}

				alpha *= 0.1f;
			}

			// If an appropriate alpha cannot be found, exit the BFGS loop.
			if (j == num_alphas) break;

			// Update Hessian matrix h.
			for (i = 0; i < num_variables; ++i) // Calculate y = g2 - g1.
				y[i] = g2[i] - g1[i];
			for (i = 0; i < num_variables; ++i) // Calculate mhy = -h * y.
			{
				float sum = 0.0f;
				for (j = 0; j < num_variables; ++j)
					sum += h[mp(i, j)] * y[j];
				mhy[i] = -sum;
			}
			yhy = 0;
			for (i = 0; i < num_variables; ++i) // Calculate yhy = -y * mhy = -y * (-hy).
				yhy -= y[i] * mhy[i];
			yp = 0;
			for (i = 0; i < num_variables; ++i) // Calculate yp = y * p.
				yp += y[i] * p[i];
			ryp = 1 / yp;
			pco = ryp * (ryp * yhy + alpha);
			for (i = 0; i < num_variables; ++i)
			for (j = i; j < num_variables; ++j) // includes i
			{
				h[mr(i, j)] += ryp * (mhy[i] * p[j] + mhy[j] * p[i]) + pco * p[i] * p[j];
			}

			// Move to the next iteration.
			x1 = x2;
			e1 = e2;
			g1 = g2;
		}

		// Accept c1 according to Metropolis criteria.
		if (e1 < e0)
		{
			r = compose_result(e1, x1);
			x0 = x1;
			e0 = e1;
		}
	}
	return 0;
}

void ligand::write_models(const path& output_ligand_path, const ptr_vector<result>& results, const vector<size_t>& representatives) const
{
	assert(representatives.size());
	assert(representatives.size() <= results.size());

	const size_t num_lines = lines.size();

	// Dump binding conformations to the output ligand file.
	using namespace std;
	boost::filesystem::ofstream ofs(output_ligand_path); // Dumping starts. Open the file stream as late as possible.
	ofs.setf(ios::fixed, ios::floatfield);
	ofs << setprecision(3);
	for (size_t i = 0; i < representatives.size(); ++i)
	{
		const result& r = results[representatives[i]];
		ofs << "MODEL     " << setw(4) << (i + 1) << '\n'
			<< "REMARK            TOTAL FREE ENERGY PREDICTED BY IDOCK:" << setw(8) << r.e       << " KCAL/MOL\n";
		for (size_t j = 0, heavy_atom = 0, hydrogen = 0; j < num_lines; ++j)
		{
			const string& line = lines[j];
			if (line.size() >= 79) // This line starts with "ATOM" or "HETATM"
			{
				const array<float, 3>& coordinate = line[77] == 'H' ? r.hydrogens[hydrogen++] : r.heavy_atoms[heavy_atom++];
				ofs << line.substr(0, 30)
					<< setw(8) << coordinate[0]
					<< setw(8) << coordinate[1]
					<< setw(8) << coordinate[2]
					<< line.substr(54, 16)
					<< setw(6) << 0
					<< line.substr(76);
			}
			else // This line starts with "ROOT", "ENDROOT", "BRANCH", "ENDBRANCH", TORSDOF", which will not change during docking.
			{
				ofs << line;
			}
			ofs << '\n';
		}
		ofs << "ENDMDL\n";
	}
}
