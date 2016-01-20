#include "vcf.h"

VCF::VCF(int thread_num_)
{
	genome_sequence = "";
	boundries_decided = false;
	if (thread_num_ == 0) {
		thread_num = 1;
	}
	else {
		thread_num = min(thread_num_, (int)thread::hardware_concurrency());
	}
	dout << "Thread Number: " << thread_num << endl;
}


VCF::~VCF()
{
}



void VCF::ReadVCF(string filename, SnpHash & pos_2_snp, VCFEntryHash& pos_2_vcf_entry) {
	if (!boundries_decided) {
		cout << "[Error] VCF::ReadVCF cannot read vcf file before read genome file" << endl;
		return;
	}

	ifstream vcf_file;
	vcf_file.open(filename.c_str());
	if (!vcf_file.good()) {
		cout << "[Error] VCF::ReadVCF can not open vcf file" << endl;
		return;
	}

	while (!vcf_file.eof()) { // alternative way is vcf_file != NULL
		string line;
		getline(vcf_file, line, '\n');
		//dout << line << endl;
		if (line.length() <= 1) continue;
		if (line[0] == '#') continue;
		auto columns = split(line, '\t');
		auto pos = atoi(columns[1].c_str()) - 1;
		auto ref = columns[3];
		auto alt = columns[4];
		auto quality = columns[6];

		if (alt.find(",") != string::npos) continue; // can not deal with multi alt yet
		//todo(Chen) deal with multi alt

		char snp_type = 'S'; 
		if (ref.length() > alt.length()) {
			snp_type = 'D';
		}
		else if (ref.length() < alt.length()) {
			snp_type = 'I';
		}

		//decide which thread to use
		int index = 0;
		for (int i = 0; i < pos_boundries.size(); i++) {
			if (pos < pos_boundries[i]) {
				index = i;
				break;
			}
		}

		pos_2_snp[index][pos].push_back(SNP(pos, snp_type, ref, alt));
		pos_2_vcf_entry[pos] = line;
	}
	vcf_file.close();
	return;
}

void VCF::ReadGenomeSequence(string filename) {
	ifstream genome_file;
	genome_file.open(filename.c_str());
	if (!genome_file.good()) {
		cout << "[Error] VCF::ReadGenomeSequence can not open fasta file" << endl;
		return;
	}

	genome_sequence = "";

	while(!genome_file.eof()) {
		string line;
		getline(genome_file, line, '\n');
		if (line.length() <= 1) continue;
		if (line[0] == '>') continue;
		genome_sequence += line;
	}
	genome_file.close();
	// boundries can get after knowing genome sequence.
	DecideBoundries();
	return;
}

void VCF::DecideBoundries() {
	int genome_size = genome_sequence.size();

	int distance = genome_size / thread_num;
	for (int i = 0; i < thread_num - 1; i++) {
		pos_boundries.push_back((i + 1)*distance);
	}
	pos_boundries.push_back(genome_size);

	// initialize two for copy
	unordered_map<int, vector<SNP> > ref_h;
	unordered_map<int, vector<SNP> > que_h;
	map<int, vector<SNP> > ref_m;
	map<int, vector<SNP> > que_m;

	for (int i = 0; i < thread_num; i++) {
		refpos_2_snp.push_back(ref_h);
		querypos_2_snp.push_back(que_h);
		refpos_snp_map.push_back(ref_m);
		querypos_snp_map.push_back(que_m);
	}

	boundries_decided = true;

}

void VCF::ReadRefVCF(string filename) {
	ReadVCF(filename, refpos_2_snp, refpos_2_vcf_entry);
}

void VCF::ReadQueryVCF(string filename) {
	ReadVCF(filename, querypos_2_snp, querypos_2_vcf_entry);
}

bool VCF::CompareSnps(SNP r, SNP q) {
	if (r.snp_type == q.snp_type && r.alt == q.alt) return true;
	return false;
}

void VCF::DirectSearchInThread(unordered_map<int, vector<SNP> > & ref_snps, unordered_map<int, vector<SNP> > & query_snps) {
	auto rit = ref_snps.begin();
	auto rend = ref_snps.end();
	for (; rit != rend;) {
		auto r_pos = rit->first;
		auto & r_snps = rit->second;
		auto qit = query_snps.find(r_pos);
		if (qit != query_snps.end()) {
			
			auto & q_snps = qit->second;

			if (r_snps.size() != 1 || q_snps.size() != 1) {
				cout << "[Error] snp vector size not right" << endl;
			}

			vector<vector<SNP>::iterator> r_deleted_snps;
			vector<vector<SNP>::iterator> q_deleted_snps;
			for (auto r_snp_it = r_snps.begin(); r_snp_it != r_snps.end(); ++r_snp_it) {
				for (auto q_snp_it = q_snps.begin(); q_snp_it != q_snps.end(); ++q_snp_it) {
					if (CompareSnps(*r_snp_it, *q_snp_it)) {
						r_deleted_snps.push_back(r_snp_it);
						q_deleted_snps.push_back(q_snp_it);
					}
				}
			}
			for (int i = 0; i < r_deleted_snps.size(); i++) {
				r_snps.erase(r_deleted_snps[i]);
			}
			if (r_snps.size() == 0) {
				rit = ref_snps.erase(rit);
			}
			else {
				++rit;
			}
			for (int i = 0; i < q_deleted_snps.size(); i++) {
				q_snps.erase(q_deleted_snps[i]);
			}
			if (q_snps.size() == 0) {
				query_snps.erase(qit);
			}
		}else{
            ++rit;
        }
	}
}

// directly match by position
void VCF::DirectSearchMultiThread() {
	vector<thread> threads;
	//spawn threads
	unsigned i = 0;
	for (; i < thread_num - 1; i++) {
		threads.push_back( thread(&VCF::DirectSearchInThread, this, ref(refpos_2_snp[i]), ref(querypos_2_snp[i])) );
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
	DirectSearchInThread(refpos_2_snp[i], querypos_2_snp[i]);

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
}

string VCF::ModifySequenceBySnp(string sequence, SNP s, int offset) {
	// [todo] unit test
	string result = "";
	int snp_pos = s.pos - offset;
	int snp_end = s.pos + s.ref.length();
	assert(snp_end <= sequence.length());
	result += sequence.substr(0, snp_pos);
	result += s.alt;
	result += sequence.substr(snp_end, sequence.length() - snp_end);
	return result;
}
string VCF::ModifySequenceBySnpList(string sequence, vector<SNP> s, int offset) {
	// [todo] unit test
	string result = "";
	int start_pos = 0;
	for (int i = 0; i < s.size(); i++) {
		int snp_pos = s[i].pos - offset;
		int snp_end = s[i].pos + s[i].ref.length();
		string snp_alt = s[i].alt;
		result += sequence.substr(start_pos, snp_pos - start_pos);
		result += snp_alt;
		start_pos = snp_end;
	}
	if (start_pos < sequence.length()) {
		result += sequence.substr(start_pos, sequence.length() - start_pos);
	}
	return result;
}

bool VCF::ComplexMatch(SNP s, vector<SNP> comb) {
	//size of comb >= 1
	sort(comb.begin(), comb.end());
	int ref_left = s.pos;
	int ref_right = ref_left + s.ref.length();

	int comb_size = comb.size();
	int que_left = comb[0].pos;
	int que_right = comb[comb_size - 1].pos + comb[comb_size - 1].ref.length();

	int genome_left = min(ref_left, que_left);
	int genome_right = max(ref_right, que_right);

	string subsequence = genome_sequence.substr(genome_left, genome_right - genome_left);
	return ModifySequenceBySnp(subsequence, s, genome_left) == ModifySequenceBySnpList(subsequence, comb, genome_left);
}

void VCF::ComplexSearchInThread(map<int, vector<SNP> > & ref_snps, map<int, vector<SNP> > & query_snps) {
	// linear algorithm
	vector<SNP> deleted_ref_snps;
	vector<SNP> deleted_que_snps;
	// for each position in ref, i.e. a vector
	for (auto rit = ref_snps.begin(); rit != ref_snps.end(); ++rit) {

		int ref_start_pos = rit->first;
		auto ref_snp_list = rit->second;
		auto qit_start = query_snps.begin();

		// for each snp in the vector
		for (int i = 0; i < ref_snp_list.size(); i++) {
			auto ref_ref = ref_snp_list[i].ref;
			auto ref_alt = ref_snp_list[i].alt;
			int ref_end_pos = ref_start_pos + ref_ref.size();
			vector<SNP> candidate_query_list;

			// find all snps in query that locate inside the ref snp
			for (auto qit = qit_start; qit != query_snps.end(); ++qit) {
				int que_start_pos = qit->first;
				auto que_snp_list = qit->second;
				if (que_start_pos >= ref_start_pos && que_start_pos < ref_end_pos) {
					for (int j = 0; j < que_snp_list.size(); j++) {
						candidate_query_list.push_back(que_snp_list[i]);
					}
					qit_start = qit;
				}
				else if (que_start_pos < ref_start_pos) {
					qit_start = qit;
				}
				else if (que_start_pos >= ref_end_pos) {
					break;
				}
			}


			int candidate_size = candidate_query_list.size();
			if (candidate_size == 0) continue;
			
			//check all combinations, from largest, one single match is enough
			for (int k = candidate_query_list.size(); k >= 1; --k) {
				vector<vector<SNP>> combinations = CreateCombinations(candidate_query_list, k);
				bool matched = false;
				// check combinations with k elements
				for (auto cit = combinations.begin(); cit != combinations.end(); ++cit) {
					auto comb = *cit;
					if (ComplexMatch(ref_snp_list[i], *cit)) {
						matched = true;

						// delete corresponding snps
						deleted_ref_snps.push_back(ref_snp_list[i]);
						auto iit = (*cit).begin();
						deleted_que_snps.insert(iit, (*cit).begin(), (*cit).end());
						break;
					}
				}
				if (matched) {
					break;
				}
			}
		}
	}

	// delete all snps, first check position, then delete by value matching
	// [todo] this actually should be one separated function

	for (auto it = deleted_ref_snps.begin(); it != deleted_ref_snps.end(); ++it) {
		auto snp = *it;
		auto pos = snp.pos;
		auto & v = ref_snps[pos];
		auto vit = v.begin();
		bool found = false;
		while (vit != v.end()) {
			if (snp == *vit) {
				vit = v.erase(vit);
				found = true;
			}
			else {
				++vit;
			}
		}
		if (v.size() == 0) {
			ref_snps.erase(ref_snps.find(pos));
		}
	}

	for (auto it = deleted_que_snps.begin(); it != deleted_que_snps.end(); ++it) {
		auto snp = *it;
		auto pos = snp.pos;
		auto & v = query_snps[pos];
		auto vit = v.begin();
		while (vit != v.end()) {
			if (snp == *vit) {
				vit = v.erase(vit);
			}
			else {
				++vit;
			}
		}
		if (v.size() == 0) {
			query_snps.erase(query_snps.find(pos));
		}
	}
}

// match by overlapping reference region
void VCF::ComplexSearchMultiThread() {
	if (GetRefSnpNumber() == 0 || GetQuerySnpNumber() == 0) return;

	// transfer data from hash to map
	for (int i = 0; i < refpos_2_snp.size(); i++) {
		auto & pos_snp_hash = refpos_2_snp[i];
		for (auto rit = pos_snp_hash.begin(); rit != pos_snp_hash.end(); ++rit) {
			refpos_snp_map[i][rit->first] = rit->second;
		}
	}
	refpos_2_snp.clear();

	for (int i = 0; i < querypos_2_snp.size(); i++) {
		auto & pos_snp_hash = querypos_2_snp[i];
		for (auto qit = pos_snp_hash.begin(); qit != pos_snp_hash.end(); ++qit) {
			querypos_snp_map[i][qit->first] = qit->second;
		}
	}
	querypos_2_snp.clear();


	vector<thread> threads;
	//spawn threads
	unsigned i = 0;
	for (; i < thread_num - 1; i++) {
		threads.push_back(thread(&VCF::ComplexSearchInThread, this, ref(refpos_snp_map[i]), ref(querypos_snp_map[i])));
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
	ComplexSearchInThread(refpos_snp_map[i], querypos_snp_map[i]);

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
}

// clustering snps
void VCF::ClusteringSnps() {}

// match by cluster
void VCF::ClusteringSearch() {}

int VCF::GetRefSnpNumber() {
	int result = 0;
	for (int i = 0; i < refpos_2_snp.size(); i++) {
		result += refpos_2_snp[i].size();
	}
	return result;
}

int VCF::GetQuerySnpNumber() {
	int result = 0;
	for (int i = 0; i < querypos_2_snp.size(); i++) {
		result += querypos_2_snp[i].size();
	}
	return result;
}
