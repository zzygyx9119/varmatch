#include "wholegenome.h"

using namespace std;

WholeGenome::WholeGenome(int thread_num_, int score_unit_, int match_mode_, int score_scheme_): DiploidVCF(thread_num_){
    chrom_num = 24;
    //thread_num = thread_num_;
    score_unit = score_unit_;
    score_scheme_ = score_scheme_;
    if(match_mode_ == 0){
        match_genotype = true;
    }else{
        match_genotype = false;
    }
    dout << "WholeGenome() Thread Number: " << thread_num << endl;
    ref_variant_by_chrid = new vector<DiploidVariant>*[chrom_num];
    que_variant_by_chrid = new vector<DiploidVariant>*[chrom_num];
    variant_cluster_by_chrid = new vector<vector<VariantIndicator>> *[chrom_num];
	for (int j = 0; j < chrom_num; j++) {
		ref_variant_by_chrid[j] = new vector<DiploidVariant>;
		que_variant_by_chrid[j] = new vector<DiploidVariant>;
		variant_cluster_by_chrid[j] = new vector<vector<VariantIndicator>>;
	}

    // chr_id starts from 0
	for(int j = 1; j <= 22; j++){
        string chr_name = to_string(j);
        chrname_dict[chr_name] = j-1;
        chr_name = "chr"+chr_name;
        chrname_dict[chr_name] = j-1;
	}
	chrname_dict["X"] = 22;
	chrname_dict["chrX"] = 22;
	chrname_dict["Y"] = 23;
	chrname_dict["chrY"] = 23;

}

WholeGenome::~WholeGenome(){

    for(int j = 0; j < chrom_num; j++){
        ref_variant_by_chrid[j]->clear();
        que_variant_by_chrid[j]->clear();
        variant_cluster_by_chrid[j]->clear();
        delete ref_variant_by_chrid[j];
        delete que_variant_by_chrid[j];
        delete variant_cluster_by_chrid[j];
    }
    delete[] ref_variant_by_chrid;
    delete[] que_variant_by_chrid;
    delete[] variant_cluster_by_chrid;
}

bool WholeGenome::ReadWholeGenomeSequence(string filename){
    std::ifstream input(filename);
    if(!input.good()){
        std::cerr << "Error opening '"<<filename<<"'. Bailing out." << std::endl;
        return false;
    }

    std::string line, name, content;
    int real_chrom_num = 0;
    while( std::getline( input, line ).good() ){
        if( line.empty() || line[0] == '>' ){ // Identifier marker
            if( !name.empty() ){ // Print out what we read from the last entry
                //std::cout << name << " : " << content << std::endl;
                if(chrname_dict.find(name) == chrname_dict.end()){
                    cout << "[VarMatch] Error: detected chromosome name: " << name <<" does not exist in human genome." << endl;
                    return false;
                }
                int chr_id = chrname_dict[name];
                chrid_by_chrname[name] = chr_id;
                chrname_by_chrid[chr_id] = name;
                genome_sequences[chr_id] = content;
                real_chrom_num++;
                name.clear();
            }
            if( !line.empty() ){
                name = split(line, ' ')[0].substr(1);
            }
            content.clear();
        } else if( !name.empty() ){
            if( line.find(' ') != std::string::npos ){ // Invalid sequence--no spaces allowed
                name.clear();
                content.clear();
            } else {
                content += line;
            }
        }
    }
    if( !name.empty() ){ // Print out what we read from the last entry
        //std::cout << name << " : " << content << std::endl;
        if(chrname_dict.find(name) == chrname_dict.end()){
            cout << "[VarMatch] Error: detected chromosome name: " << name <<" does not exist in human genome." << endl;
            return false;
        }
        int chr_id = chrname_dict[name];
        chrid_by_chrname[name] = chr_id;
        chrname_by_chrid[chr_id] = name;
        genome_sequences[chr_id] = content;
        real_chrom_num++;
    }
    // test

    chrom_num = real_chrom_num;
    dout << "detected chromosome num: " << chrom_num << endl;
//    for(auto it = genome_sequences.begin(); it != genome_sequences.end(); ++it){
//        cout << it->first << ":" << (it->second).length();
//    }
    return true;
}

bool WholeGenome::ReadGenomeSequenceList(string filename){

}

int WholeGenome::ReadWholeGenomeVariant(string filename, int flag){
    int total_num = 0;
	ifstream vcf_file;
	vcf_file.open(filename.c_str());
	if (!vcf_file.good()) {
		cout << "[VarMatch] Error: can not open vcf file" << endl;
		return -1;
	}
	int genotype_index = -1;
	char genotype_separator = '/';
	//int genome_sequence_length = genome_sequence.length();
	while (!vcf_file.eof()) { // alternative way is vcf_file != NULL
		string line;
		getline(vcf_file, line, '\n');
		// check ineligible lines
		//dout << line << endl;
		if ((int)line.length() <= 1) continue;
		//if (line.find_first_not_of(' ') == std::string::npos) continue;

		if (line[0] == '#') {
			continue;
		}
		auto columns = split(line, '\t');
		if (columns.size() < 10) {
			if(match_genotype){
                cout << "[VarMatch] Warning: not enough information in VCF file for genotype matching." << endl;
                cout << "[VarMatch] \tAutomatically turn off genotype matching module " << filename << endl;
                match_genotype = false;
                continue;
            }
            if(columns.size() < 6){
                cout << "[VarMatch] Warning: not enough information in VCF file for variant matching." << endl;
                cout << "[VarMatch] skip current variant: " << line << endl;
                continue;
            }
		}
		string chr_name = columns[0];
		auto pos = atoi(columns[1].c_str()) - 1; // 0-based coordinate

		auto ref = columns[3];
		auto alt_line = columns[4];
		auto quality = columns[5];

		ToUpper(ref);
		ToUpper(alt_line);

		bool is_heterozygous_variant = false;
		bool is_multi_alternatives = false;

		if (columns.size() >= 10) {
			if (genotype_index < 0) {
                auto formats = split(columns[8], ':');
                for (int i = 0; i < formats.size(); i++) {
                    if (formats[i] == "GT") {
                        genotype_index = i;
                        break;
                    }
                }
                if(genotype_index < 0){
                    cout << "[VarMatch] VCF entry does not contain genotype information" << endl;
                    continue;
                }
			}
			auto additionals = split(columns[9], ':');
            vector<string> genotype_columns = split(additionals[genotype_index], genotype_separator);

            if(genotype_columns.size() != 2){
                genotype_separator = '|';
                genotype_columns = split(additionals[genotype_index], genotype_separator);
            }

			// normalize format of genotype: sorted, separated by |
			if (genotype_columns.size() != 2) {
				cout << "[VarMatch] Warning Unrecognized Genotype: " << additionals[genotype_index] << endl;
				continue;
			}
			else {
				if (genotype_columns[0] != genotype_columns[1]) {
					is_heterozygous_variant = true;
				}
			}

            if (genotype_columns[1] == "0" && genotype_columns[0] == "0" && match_genotype) {
                continue;
            }
		}

		vector<string> alt_list;
		if (alt_line.find(",") != std::string::npos) {
			alt_list = split(alt_line, ',');
			is_multi_alternatives = true;
		}
		else {
			alt_list.push_back(alt_line);
		}

        int snp_ins = max(0, (int)alt_list[0].length() - (int)ref.length());
        int snp_del = max(0, (int)ref.length() - (int)alt_list[0].length());
        if(is_multi_alternatives){
            snp_ins = max(snp_ins, (int)alt_list[1].length() - (int)ref.length());
            snp_del = max(snp_del, (int)ref.length() - (int)alt_list[1].length());
        }

        if(snp_ins > VAR_LEN || snp_del > VAR_LEN){
            //dout << "[VarMatch] skip large INDEL with length > 50 bp" << endl;
            continue;
        }
		DiploidVariant dv(pos, ref, alt_list, is_heterozygous_variant, is_multi_alternatives, snp_del, snp_ins, flag);
		if (normalization) {
			NormalizeDiploidVariant(dv);
		}
        if(chrid_by_chrname.find(chr_name) != chrid_by_chrname.end()){
            int chr_id = chrid_by_chrname[chr_name];
            if(flag == 0){
                ref_variant_by_chrid[chr_id]->push_back(dv);
            }else{
                que_variant_by_chrid[chr_id]->push_back(dv);
            }
        }else{
            int chr_id = chrname_dict[chr_name];
            if(flag == 0){
                ref_variant_by_chrid[chr_id]->push_back(dv);
            }else{
                que_variant_by_chrid[chr_id]->push_back(dv);
            }
        }

        total_num++;
	}
	vcf_file.close();
	return total_num;
}

bool WholeGenome::ReadVariantFileList(string filename){

}

int WholeGenome::ScoreEditDistance(DiploidVariant & dv, int allele_indicator){
    return EditDistance(dv.ref, dv.alts[allele_indicator]);
}

inline int WholeGenome::EditDistance(const std::string& s1, const std::string& s2)
{
	const std::size_t len1 = s1.size(), len2 = s2.size();
	std::vector<unsigned int> col(len2+1), prevCol(len2+1);

	for (unsigned int i = 0; i < prevCol.size(); i++)
		prevCol[i] = i;
	for (unsigned int i = 0; i < len1; i++) {
		col[0] = i+1;
		for (unsigned int j = 0; j < len2; j++)
                        // note that std::min({arg1, arg2, arg3}) works only in C++11,
                        // for C++98 use std::min(std::min(arg1, arg2), arg3)
			col[j+1] = std::min({ prevCol[1 + j] + 1, col[j] + 1, prevCol[j] + (s1[i]==s2[j] ? 0 : 1) });
		col.swap(prevCol);
	}
	return prevCol[len2];
}

bool WholeGenome::ParallelClustering(){
    // parallel by chr
    int parallel_steps = chrom_num / thread_num;
    if(parallel_steps*thread_num < chrom_num) parallel_steps += 1;
    int chr_id = 0;
    for(int i = 0; i < parallel_steps; i++){
        vector<thread> threads;
        for(int j = 0; j < thread_num-1 && chr_id < chrom_num-1; j++){
            if(chrname_by_chrid.find(chr_id) != chrname_by_chrid.end()){
                threads.push_back(thread(&WholeGenome::SingleThreadClustering, this, chr_id));
            }
            chr_id ++;
        }
        if(chr_id < chrom_num){
            if(chrname_by_chrid.find(chr_id) != chrname_by_chrid.end()){
                SingleThreadClustering(chr_id);
            }
            chr_id ++;
        }
        std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
        threads.clear();
    }


    for(int i = 0; i < chrom_num; i++){
        variants_by_cluster.insert(variants_by_cluster.end(), variant_cluster_by_chrid[i]->begin(), variant_cluster_by_chrid[i]->end());
    }

    // test output
    dout << endl;
    map<int, int> size_num;
    map<int, int> size_chrid;
    for(int i = 0; i < chrom_num; i++){
        dout << i << ": " << variant_cluster_by_chrid[i]->size() << endl;
        for(int j = 0; j < variant_cluster_by_chrid[i]->size(); j++){
            int temp_size = variant_cluster_by_chrid[i]->at(j).size();
            if(size_num.find(temp_size) != size_num.end()){
                size_num[temp_size] ++;
            }else{
                size_num[temp_size] = 1;
            }
            if(size_chrid.find(temp_size) == size_chrid.end()){
                size_chrid[temp_size] = i;
            }
        }
    }

    cout << endl;
    for(auto it = size_num.begin(); it != size_num.end(); ++it){
        dout << it->first << ": " << it->second << endl;
    }

//    cout << endl;
//    cout << "size and location:" << endl;
//    for(auto it = size_chrid.begin(); it != size_chrid.end(); ++it){
//        dout << it->first << ": " << it->second << endl;
//    }

    return true;
}

bool WholeGenome::ParallelMatching(){

}

bool WholeGenome::TBBMatching()
{

}

bool WholeGenome::ClusteringMatchInThread(int start, int end, int thread_index) {

	for (int cluster_id = start; cluster_id < end; cluster_id++) {
        if(cluster_id >= variants_by_cluster.size()) break;
        //dout << cluster_id << endl;
        //bool method1 = MatchingSingleCluster(cluster_id, thread_index);
        bool method2 = MatchingSingleClusterBaseExtending(cluster_id, thread_index);
        //if(method1 != method2){
        //    cout << "not same result for cluster :" << cluster_id << ": " << method1 << "," << method2 << endl;
        //}

	}
	return true;
}


// to reduce memory usage of paths, move all functions about SequencePath out into WholeGenome with a parameter SequencePath
bool WholeGenome::PathNeedDecision(SequencePath& sp, multimap<int, int> * choices_by_pos[], int pos){
    for(int i = 0; i < 2; i++){
        if(choices_by_pos[i]->find(pos) != choices_by_pos[i]->end()){
            // you need to make choices now
            if(sp.choice_made[i].find(pos) == sp.choice_made[i].end()){
                // no choice made at current pos
                return true;
            }
        }
    }
    return false;
}

int WholeGenome::CheckPathEqualProperty(SequencePath & sp)
{

    if(match_genotype){
        //bool equal_sequences = false;
        // same ref position, same donor length, same donor sequence, keep
        if(sp.donor_sequences[0].length() == sp.donor_sequences[2].length() &&
           sp.donor_sequences[1].length() == sp.donor_sequences[3].length()){
            if(sp.donor_sequences[0] == sp.donor_sequences[2] && sp.donor_sequences[1] == sp.donor_sequences[3]){
                sp.same_donor_len = true;
                sp.current_equal_donor_pos[0] = sp.donor_sequences[0].length()-1;
                sp.current_equal_donor_pos[1] = sp.donor_sequences[1].length()-1;
                return 0;
            }else{
                //dout << "delete this path at pos: " << sp.current_genome_pos << " for not equal donor sequence";
                //PrintPath(sp);
                return -1;
            }
        }else{
            sp.same_donor_len = false;
            int min_donor_identical_len[2];
            for(int i = 0; i < 2; i++){
                // compare each strain
                min_donor_identical_len[i] = min(sp.donor_sequences[0+i].length(), sp.donor_sequences[2+i].length());
                for(int k = sp.current_equal_donor_pos[i]+1; k < min_donor_identical_len[i]; k++){
                    if(sp.donor_sequences[0+i][k] != sp.donor_sequences[2+i][k]){
                        return -1;
                    }
                }
                sp.current_equal_donor_pos[i] = min_donor_identical_len[i]-1;
            }
            return 0;
        }
    }else{
        if(sp.donor_sequences[0].length() == sp.donor_sequences[2].length()){
            if(sp.donor_sequences[0] == sp.donor_sequences[2]){
                sp.same_donor_len = true;
                sp.current_equal_donor_pos[0] = sp.donor_sequences[0].length()-1;
                //sp.current_equal_donor_pos[1] = sp.donor_sequences[1].length()-1;
                return 0;
            }else{
                //dout << "delete this path at pos: " << sp.current_genome_pos << " for not equal donor sequence";
                //PrintPath(sp);
                return -1;
            }
        }else{
            sp.same_donor_len = false;
            int min_donor_identical_len[2];
            //for(int i = 0; i < 2; i++)
            int i = 0;
            {
                // compare each strain
                min_donor_identical_len[i] = min(sp.donor_sequences[0+i].length(), sp.donor_sequences[2+i].length());
                for(int k = sp.current_equal_donor_pos[i]+1; k < min_donor_identical_len[i]; k++){
                    if(sp.donor_sequences[0+i][k] != sp.donor_sequences[2+i][k]){
                        return -1;
                    }
                }
                sp.current_equal_donor_pos[i] = min_donor_identical_len[i]-1;
            }
            return 0;
        }
    }
}

// one step is not one nt, but to the next sync point
// i.e. one step, one sync point
int WholeGenome::PathExtendOneStep(SequencePath& sp,
                                   multimap<int, int> * choices_by_pos[],
                                   const string & reference_sequence,
                                   vector<int> & sync_points){
    //-1 operation fail, path deleted
    //0 operation succeed
    //1 operation fail, need to make decision first, then extend
    //2 path reached end, need to check if good

    if(sp.reached_sync_num >= sync_points.size()) return -1;

    int start_pos = sp.current_genome_pos + 1;
    int end_pos = sync_points[sp.reached_sync_num]; // the next sync point, end pos included

    for(int next_genome_pos = start_pos; next_genome_pos <= end_pos; next_genome_pos++){

        // before make decision, we need to check if the equal property still holds
        if(PathNeedDecision(sp, choices_by_pos, next_genome_pos)){

            // check equal property
            int statu = CheckPathEqualProperty(sp);
            if(statu == -1) return -1;
            return 1; // need decision on next position
        }

        // else extend one nt
        for(int i = 0; i < 4; i++){

            if(! match_genotype){
                if(i%2 != 0) continue;
            }

            if(sp.string_sequences[i][next_genome_pos] == "."){
                sp.donor_sequences[i] += reference_sequence[next_genome_pos];
            }else{
                sp.donor_sequences[i] += sp.string_sequences[i][next_genome_pos];
            }
        }
        sp.current_genome_pos = next_genome_pos;
    }

    // reaches the end of end_pos
    sp.reached_sync_num ++;

    if(sp.reached_sync_num >= sync_points.size()){
        // last sync point is the end of ref genome sequence
        if(sp.donor_sequences[0] == sp.donor_sequences[2] &&
           sp.donor_sequences[1] == sp.donor_sequences[3]){
            return 2;
       }else{
            //dout << "delete this path at pos: " << sp.current_genome_pos << " for reach end but not equal";
            //PrintPath(sp);
            return -1;
       }
    }
    return CheckPathEqualProperty(sp);
    // first try to converge, then extend

}

int WholeGenome::CalculateScore(DiploidVariant & dv, int choice){
    int score = 0;
    if(score_unit == 0){
        score = 1;
    }else if(score_unit == 1){
        if(match_genotype){
            if(choice == -1){
                score += ScoreEditDistance(dv, 0);
            }else if(choice == 0){
                score += ScoreEditDistance(dv, 0);
                if(dv.multi_alts){
                    score += ScoreEditDistance(dv, 1);
                }
            }else{
                score += ScoreEditDistance(dv, 0);
                score += ScoreEditDistance(dv, 1);
            }
        }else{
            score += ScoreEditDistance(dv, choice);
        }
    }

    if(score_scheme == 0){
        return score;
    }else if(score_scheme == 1 || score_scheme == 2){
        if(dv.flag == score_scheme-1){
            return score;
        }else{
            return 0;
        }
    }
}

// no genotype means you can maintain only one strand
// for simplicity, also work on original SequencePath data structure
// when making decision, only decide one path
// when extending, only extend one path
// when comparing, only compare one path
bool WholeGenome::PathMakeDecisionNoGenotype(SequencePath& sp,
                                 vector<DiploidVariant> & variant_list,
                                 multimap<int, int> * choices_by_pos[],
                                 list<SequencePath> & sequence_path_list,
                                 const string & reference_sequence)
{
    int pos = sp.current_genome_pos+1;
    vector<pair<int, int>> candidate_choices[2];
    for(int i = 0; i < 2; i++){
        // because if it's (-1,-1), it will do nothing, so it's ok to have this one...
        candidate_choices[i].push_back(pair<int, int>(-1, -1));
        // to maintain existance
        // in this position, make choice of not use any variants, no matter if there is variant

        pair<multimap<int, int>::iterator, multimap<int, int>::iterator> var_range;
        var_range = choices_by_pos[i]->equal_range(pos);

        for(auto it = var_range.first; it != var_range.second; ++it){
            int var_index = (*it).second;
            DiploidVariant var = variant_list[var_index];
            // check if current var influence
            string ref = var.ref; //even we do not know the offset, we know ref start from pos of reference_sequence
            string alts[2];
            alts[0] = var.alts[0];
            alts[1] = alts[0];
            if(var.multi_alts){
                alts[1] = var.alts[1];
            }

            // not just purely consider if a vqriant can be applied, but if a choice
            bool choice_applicable = true;
            for(int k = 0; k < ref.length(); k++){
            // for each ref char
                int y = 0;
                // for each strain
                if(sp.string_sequences[i*2+y][k+pos] != "."){
                    // decision in this area has already been made
                    if(k >= alts[y].length()){
                        choice_applicable = false;
                        break;
                    }else{
                        if(ref[k] != alts[y][k]){
                            choice_applicable = false;
                            break;
                        }
                    }
                }
            }

            if(choice_applicable){
                candidate_choices[i].push_back(pair<int, int>(var_index, 0));
            }

            if(var.multi_alts){

                //if heterozygous, then there is another choice, check if it is applicable
                string temp = alts[0];
                alts[0] = alts[1];
                alts[1] = temp;

                choice_applicable = true;
                for(int k = 0; k < ref.length(); k++){
                // for each ref char
                    //for(int y = 0; y < 2; y++)
                    int y = 0;
                    // for each strain
                    if(sp.string_sequences[i*2+y][k+pos] != "."){
                        // decision in this area has already been made
                        if(k >= alts[y].length()){
                            // should be a deletion
                            choice_applicable = false;
                            break;
                        }else{
                            // should be equal at current position
                            // can be an insertion, as long as current position is the same
                            if(ref[k] != alts[y][k]){
                                choice_applicable = false;
                                break;
                            }
                        }
                    }
                }

                if(choice_applicable){
                    candidate_choices[i].push_back(pair<int, int>(var_index, 1));
                }
            }
        }
    }

    //dout << candidate_choices[0].size() << "," << candidate_choices[1].size() << endl;

    for(int i = 0; i < candidate_choices[0].size(); i++){
        for(int j = 0; j < candidate_choices[1].size(); j++){
            // iterate all choices
            SequencePath path = sp;
            pair<int, int> var_choice[2];
            var_choice[0] = candidate_choices[0][i];
            var_choice[1] = candidate_choices[1][j];
            for(int x = 0; x < 2; x++){
                // iterate truth and predict
                int var_index = var_choice[x].first;
                if(var_index != -1){
                    DiploidVariant var = variant_list[var_index];
                    if(var.flag != x){
                        dout << "Error" << endl;
                    }
                    string ref = var.ref;
                    string alts[2];
                    int c = var_choice[x].second;
                    alts[0] = var.alts[c];
                    path.score += CalculateScore(var, c);

                    ToUpper(ref);
                    ToUpper(alts[0]);
                    int y = 0;

                    int k = 0;
                    for(; k < ref.length()-1; k++){
                        if(k < alts[y].length()){
                            if(ref[k] != alts[y][k]){
                                path.string_sequences[x*2+y][pos+k] = alts[y].substr(k,1);
                            }
                            // else change nothing
                        }else{
                            path.string_sequences[x*2+y][pos+k] = "";
                        }
                    }
                    // hence k == ref.length()-1, the last position
                    if(k < alts[y].length()){
                        string alt_part = alts[y].substr(k, alts[y].length()-k);
                        if(alt_part.length() > 1){
                            if(alt_part[0] == ref[k]){
                                if(path.string_sequences[x*2+y][pos+k] == "."){
                                    path.string_sequences[x*2+y][pos+k] = alt_part;
                                }else{
                                    path.string_sequences[x*2+y][pos+k] += alt_part.substr(1, alt_part.size() - 1);
                                }
                            }else{
                                path.string_sequences[x*2+y][pos+k] = alt_part;
                            }
                        }else{
                            if(ref[k] != alts[y][k]){
                                path.string_sequences[x*2+y][pos+k] = alt_part;
                            }
                        }
                    }else{
                        path.string_sequences[x*2+y][pos+k] = "";
                    }

                }
                path.choice_made[x][pos] = var_choice[x];
            }
            sequence_path_list.push_back(path);
        }
    }

    //expected number of inserted paths are 2,3,4,6,x...
    return true;
}


bool WholeGenome::PathMakeDecision(SequencePath& sp,
                                 vector<DiploidVariant> & variant_list,
                                 multimap<int, int> * choices_by_pos[],
                                 list<SequencePath> & sequence_path_list,
                                 const string & reference_sequence)
{
    int pos = sp.current_genome_pos+1;

    vector<pair<int, int>> candidate_choices[2];
    for(int i = 0; i < 2; i++){

        // because if it's (-1,-1), it will do nothing, so it's ok to have this one...
        candidate_choices[i].push_back(pair<int, int>(-1, -1));
        // in this position, make choice of not use any variants, no matter if there is variant

        pair<multimap<int, int>::iterator, multimap<int, int>::iterator> var_range;
        var_range = choices_by_pos[i]->equal_range(pos);

        for(auto it = var_range.first; it != var_range.second; ++it){
            int var_index = (*it).second;
            DiploidVariant var = variant_list[var_index];
            //PrintVariant(var);

            // check if current var influence
            string ref = var.ref; //even we do not know the offset, we know ref start from pos of reference_sequence
            string alts[2];
            alts[0] = var.alts[0];
            alts[1] = alts[0];
            if(var.multi_alts){
                alts[1] = var.alts[1];
            }else if(var.heterozygous){
                alts[1] = ref;
            }

            // not just purely consider if a vqriant can be applied, but if a choice

            bool choice_applicable = true;
            for(int k = 0; k < ref.length(); k++){
            // for each ref char
                for(int y = 0; y < 2; y++){
                    // for each strain
                    if(sp.string_sequences[i*2+y][k+pos] != "."){
                        // decision in this area has already been made
                        if(k >= alts[y].length()){
                            choice_applicable = false;
                            break;
                        }else{
                            if(ref[k] != alts[y][k]){
                                choice_applicable = false;
                                break;
                            }
                        }
                    }
                }
                if(!choice_applicable) break;
            }

            if(choice_applicable){
                candidate_choices[i].push_back(pair<int, int>(var_index, 0));
            }

            if(var.heterozygous){

                //if heterozygous, then there is another choice, check if it is applicable
                string temp = alts[0];
                alts[0] = alts[1];
                alts[1] = temp;

                choice_applicable = true;
                for(int k = 0; k < ref.length(); k++){
                // for each ref char
                    for(int y = 0; y < 2; y++){
                        // for each strain
                        if(sp.string_sequences[i*2+y][k+pos] != "."){
                            // decision in this area has already been made
                            if(k >= alts[y].length()){
                                // should be a deletion
                                choice_applicable = false;
                                break;
                            }else{
                                // should be equal at current position
                                // can be an insertion, as long as current position is the same
                                if(ref[k] != alts[y][k]){
                                    choice_applicable = false;
                                    break;
                                }
                            }
                        }
                    }
                    if(!choice_applicable) break;
                }

                if(choice_applicable){
                    if(var.multi_alts){
                        candidate_choices[i].push_back(pair<int, int>(var_index, 1));
                    }else{
                        candidate_choices[i].push_back(pair<int, int>(var_index, -1));
                    }
                }
            }
        }
    }

    //dout << candidate_choices[0].size() << "," << candidate_choices[1].size() << endl;

    for(int i = 0; i < candidate_choices[0].size(); i++){
        for(int j = 0; j < candidate_choices[1].size(); j++){
            // iterate all choices
            SequencePath path = sp;
            pair<int, int> var_choice[2];
            var_choice[0] = candidate_choices[0][i];
            var_choice[1] = candidate_choices[1][j];
            for(int x = 0; x < 2; x++){
                // iterate truth and predict
                int var_index = var_choice[x].first;
                if(var_index != -1){
//                    string temp_sequence = reference_sequence.substr(pos, 1);
//                    path.string_sequences[x*2][pos] = temp_sequence;
//                    path.string_sequences[x*2+1][pos] = temp_sequence;
//                }else{
                    // set score


                    DiploidVariant var = variant_list[var_index];
                    if(var.flag != x){
                        dout << "Error" << endl;
                    }
                    string ref = var.ref;
                    string alts[2];

                    int c = var_choice[x].second;
                    if(c == -1){
                        alts[0] = ref;
                        alts[1] = var.alts[0];
                    }else{
                        // c == 0 or 1
                        alts[0] = var.alts[c];
                        alts[1] = alts[0];

                        if(var.multi_alts){
                            // choose 1 or 0
                            alts[1] = var.alts[1- c];
                        }else{
                            // c is 0, choose 0 or -1
                            if(var.heterozygous) alts[1] = ref;
                        }
                    }

                    path.score += CalculateScore(var, c);

                    ToUpper(ref);
                    ToUpper(alts[0]);
                    ToUpper(alts[1]);
                    for(int y = 0; y < 2; y++){
                        // iterate two alts
                        int k = 0;
                        for(; k < ref.length()-1; k++){
                            if(k < alts[y].length()){
                                if(ref[k] != alts[y][k]){
                                    path.string_sequences[x*2+y][pos+k] = alts[y].substr(k,1);
                                }
                                // else change nothing
                            }else{
                                path.string_sequences[x*2+y][pos+k] = "";
                            }
                        }
                        // hence k == ref.length()-1, the last position
                        if(k < alts[y].length()){
                            string alt_part = alts[y].substr(k, alts[y].length()-k);
                            if(alt_part.length() > 1){
                                if(alt_part[0] == ref[k]){
                                    if(path.string_sequences[x*2+y][pos+k] == "."){
                                        path.string_sequences[x*2+y][pos+k] = alt_part;
                                    }else{
                                        path.string_sequences[x*2+y][pos+k] += alt_part.substr(1, alt_part.size() - 1);
                                    }
                                }else{
                                    path.string_sequences[x*2+y][pos+k] = alt_part;
                                }
                            }else{
                                if(ref[k] != alts[y][k]){
                                    path.string_sequences[x*2+y][pos+k] = alt_part;
                                }
                            }
                        }else{
                            path.string_sequences[x*2+y][pos+k] = "";
                        }
                    }
                }
                path.choice_made[x][pos] = var_choice[x];
            }
            // choice made
            //dout << "after decision at pos " << pos << endl;
            //PrintPath(path);
            sequence_path_list.push_back(path);
        }
    }

    //expected number of inserted paths are 2,3,4,6,x...
    return true;
}

void WholeGenome::PrintPath(SequencePath & sp){
    cout << "- Sequence Path:" << endl;
    cout << "@ String Sequences:" << endl;
    for(int i = 0; i < 4; i++){
        for(int j = 0; j < sp.string_sequences[i].size(); j++){
            cout << sp.string_sequences[i][j] << " ";
        }
        cout << endl;
    }
    cout << "@ Donor Sequences:" << endl;
    for(int i = 0; i < 4; i++){
        cout << sp.donor_sequences[i] << endl;
    }
    cout << "@ Removable: " << sp.removable << endl;
}

// next: while until current path list is empty
// if extend, add to next path list
// if need decision, make decision, append to current list
// if reach end, compare with best path
bool WholeGenome::MatchingSingleClusterBaseExtending(int cluster_index, int thread_index){
    //--------------for unit test------------------------------
    //dout << variant_list.size() << endl;

    //int chr_id = 0;
    //-------------end unit test-------------------------------

    vector<VariantIndicator> vi_list = variants_by_cluster[cluster_index];
    if(vi_list.size() <= 1) return false;
    // create variant_list from vi_list;
    vector<DiploidVariant> variant_list;
    int chr_id = -1;
    for(int i = 0; i < vi_list.size(); i++){
        VariantIndicator vi = vi_list[i];
        chr_id = vi.chr_id;
        int var_id = vi.var_id;
        if(vi.refer){
            variant_list.push_back(ref_variant_by_chrid[chr_id]->at(var_id));
        }else{
            variant_list.push_back(que_variant_by_chrid[chr_id]->at(var_id));
        }
    }
    if(chr_id == -1 || chr_id >= chrom_num){
        cout << "[VarMatch] Error in matching single cluster" << endl;
        return false;
    }

    //===================================================
    sort(variant_list.begin(), variant_list.end());
    // decide reference sequence
    vector<DiploidVariant> separate_var_list[2];
    vector<Interval> intervals;
	// separate into ref and que
	int total_mil = 0;
	int total_mdl = 0;
	int min_pos = genome_sequences[chr_id].length() + 1;
	int max_pos = -1;
	for (int i = 0; i < variant_list.size(); i++) {
		int flag = variant_list[i].flag; // flag indicate if the variant is from ref set(0) or query set(1)
		int pos = variant_list[i].pos;
		separate_var_list[flag].push_back(variant_list[i]);
		total_mil += variant_list[i].mil;
		total_mdl += variant_list[i].mdl;
		auto ref_sequence = variant_list[i].ref;
		auto alt_sequences = variant_list[i].alts;
		min_pos = min(pos, min_pos);
		max_pos = max((int)(pos + ref_sequence.length()), max_pos);

		int end_pos = pos + ref_sequence.length() - 1; // included end position!!
		intervals.push_back(Interval(pos, end_pos));
	}
	min_pos = max(min_pos - 1, 0);
	max_pos = min(max_pos + 1, (int)genome_sequences[chr_id].length()); //exclusive

	if (separate_var_list[0].size() == 0 || separate_var_list[1].size() == 0) {
		//dout << separate_var_list[0].size() << ", " << separate_var_list[1].size() << endl;
		return false;
	}
	if (separate_var_list[0].size() == 1 && separate_var_list[1].size() == 1){
        // try direct match to save time
        if(separate_var_list[0][0] == separate_var_list[1][0]){
            complex_ref_match_num[thread_index]++;
            complex_que_match_num[thread_index]++;

            DiploidVariant tv = separate_var_list[0][0];
            string match_record = to_string(tv.pos+1) + "\t" + tv.ref + "\t" + tv.alts[0];
            if(tv.multi_alts) match_record += "/" + tv.alts[1];
            match_record += "\t.\t.\t.\t.\t.\n";
            complex_match_records[thread_index]->push_back(match_record);
            // output match result
            return true;
        }
        // if not match, still can match by changing genome
	}else if(separate_var_list[0].size() == 1 || separate_var_list[1].size() == 1){
        int flag = 0;
        if(separate_var_list[1].size() == 1) flag = 1;
        int r_flag = 1-flag;
        if(separate_var_list[r_flag].size() > 4){
            int total_r_mdl = 0;
            int total_r_mil = 0;

            for(int k = 0; k < separate_var_list[r_flag].size(); k++){
                DiploidVariant var = separate_var_list[r_flag][k];
                int var_mdl = var.mdl;
                int var_mil = var.mil;
                int ref_length = var.ref.length();
                total_r_mdl += var_mdl;
                total_r_mil += var_mil;
            }

            if(max(separate_var_list[flag][0].mdl, separate_var_list[flag][0].mil) > max(total_r_mdl, total_r_mil)) return false;
        }
	}

	// remove singular variant
	// [todo] try removing this filter to see running time changes
    vector<bool> appliable_flag;
    int total_change = total_mil+total_mdl;

    for(int k = 0; k < variant_list.size(); k++){
        DiploidVariant cur_var = variant_list[k];
        int max_change = max(cur_var.mil, cur_var.mdl);
        if(max_change > total_change-max_change){
            appliable_flag.push_back(false);
            //dout << "this variant is removed" << endl;
        }else{
            appliable_flag.push_back(true);
        }
    }

	string subsequence = genome_sequences[chr_id].substr(min_pos, max_pos - min_pos);

	ToUpper(subsequence); // subsequence only contains upper char
	int offset = min_pos;
	int subsequence_length = max_pos - min_pos;

	// have subsequence in hand
    //generate decision point
    multimap<int, int> * choices_by_pos[2];
    // choice by pos is to also equal to var by pos
    for(int i = 0; i < 2; i++){
        choices_by_pos[i] = new multimap<int, int>();
    }

    for(int index = 0; index < variant_list.size(); index++){
        if(!appliable_flag[index]) continue;
        // remove decision point if not applicable
        int pos = variant_list[index].pos - offset;
        int flag = variant_list[index].flag;
        choices_by_pos[flag]->insert(pair<int, int>(pos, index));
        //dout << pos << index << endl;
    }

    vector<Interval> mergered_intervals = merge(intervals);
//    unordered_map<int, bool> sync_points;
//    for(int i = 0; i < mergered_intervals.size(); i++){
//        sync_points[mergered_intervals[i].end-offset] = true;
//    }
    vector<int> sync_points;
    for(int i = 0; i < mergered_intervals.size(); i++){
        sync_points.push_back(mergered_intervals[i].end-offset);
    }

    if(sync_points.back() < subsequence.size() - 1){
        sync_points.push_back(subsequence.size()-1);
    }

    // so a legal sync_points vector contains at least two
    // first is the end of variant, there should be at least one variant
    // second is the end of subsequence, there should be at least one nt not influenced by a variant

    list<SequencePath> current_path_list;
    list<SequencePath> next_path_list;
    SequencePath sp(subsequence.length());
    SequencePath best_path = sp;
    current_path_list.push_back(sp);
    while(current_path_list.size() != 0){
        bool reach_sync_point = true;
        while(current_path_list.size() != 0){
            SequencePath path = current_path_list.front();
            current_path_list.pop_front();
            //dout << path.current_genome_pos << ":" << current_path_list.size() << endl;
            //PrintPath(path);
            int is_extend = PathExtendOneStep(path, choices_by_pos, subsequence, sync_points);
            //PrintPath(path);
            if(is_extend == -1){
                continue;
            }
            else if(is_extend == 0){
                next_path_list.push_back(path);
                // here the path is supposed to reach the next sync point
            }else if(is_extend == 1){
                if(match_genotype){
                    PathMakeDecision(path, variant_list, choices_by_pos, current_path_list, subsequence);
                }else{
                    PathMakeDecisionNoGenotype(path, variant_list, choices_by_pos, current_path_list, subsequence);
                }
            }else if(is_extend == 2){
                if(path.score > best_path.score){
                    best_path = path; // only when you reach the very end can you be considered as best path
                    //PrintPath(best_path);
                }
            }
        }
        current_path_list = next_path_list;
        next_path_list.clear();
        if(current_path_list.size() > 0){
            //int current_genome_pos = current_path_list.front().current_genome_pos;
            // after revise, we do not need this check
            //if(sync_points.find(current_genome_pos) != sync_points.end()){
                //dout << "converge paths at position: " << current_genome_pos << endl;
                //dout << "before converge: " << current_path_list.size() << endl;
                ConvergePaths(current_path_list);
                //dout << "after converge: " << current_path_list.size() << endl;
            //}
        }
    }
    // print best_path
    if(best_path.score <= 0) return false;

    //dout << "new method: " << best_path.score << endl;

    //==========================output ======================
    if(match_genotype){
        ConstructMatchRecord(best_path,
                             variant_list,
                             subsequence,
                             offset,
                             thread_index,
                             chr_id);
    }else{
        ConstructMatchRecordNoGenotype(best_path,
                                       variant_list,
                                       subsequence,
                                       offset,
                                       thread_index,
                                       chr_id);
    }
    return true;
}

void WholeGenome::ConstructMatchRecord(SequencePath & best_path,
                                       vector<DiploidVariant> & variant_list,
                                       string & subsequence,
                                       int offset,
                                       int thread_index,
                                       int chr_id){
    int truth_num = 0;
    int predict_num = 0;

    bool multiple_match = false;
    if(best_path.donor_sequences[0] == best_path.donor_sequences[1]) multiple_match = true;


    string parsimonious_ref = subsequence;
    string parsimonious_alt0 = best_path.donor_sequences[0];
    string parsimonious_alt1 = best_path.donor_sequences[1];

    int parsimonious_pos = NormalizeVariantSequence(offset,
                             parsimonious_ref,
                             parsimonious_alt0,
                             parsimonious_alt1,
                             chr_id);

    string match_record = to_string(parsimonious_pos+1) + "\t" + parsimonious_ref + "\t" + parsimonious_alt0;
    if(multiple_match) match_record += "/" + parsimonious_alt1;

    string vcf_record[2];
    string phasing_record[2];

	for (int i = 0; i < 2; i++) {
		for (auto it = best_path.choice_made[i].begin(); it != best_path.choice_made[i].end(); ++it) {
            pair<int, int> selection = it->second;
            int phasing = selection.second;
            if(selection.first == -1) continue;
            if (phasing == -1) phasing = 1;
            DiploidVariant variant = variant_list[selection.first];
            if(variant.flag == 0){
                truth_num++;
            }else{
                predict_num++;
            }
            string alt_string = variant.alts[0];
            if(variant.multi_alts){
                alt_string += "/" + variant.alts[1];
            }
            string phasing_string = "";
            if(phasing == 0){
                phasing_string += "1";
                if(variant.heterozygous){
                    if(variant.multi_alts){
                        phasing_string += "|2";
                    }else{
                        phasing_string += "|0";
                    }
                }else{
                    phasing_string += "|1";
                }
            }else if(phasing == 1){
                if(variant.multi_alts){
                    phasing_string += "2|1";
                }else{
                    phasing_string += "0|1";
                }
            }
            string variant_record = to_string(variant.pos+1) + "," + variant.ref + "," + alt_string;
            vcf_record[i] += variant_record;
            phasing_record[i] += phasing_string;
            vcf_record[i] += ";";
            phasing_record[i] += ";";
		}
        vcf_record[i] = vcf_record[i].substr(0, vcf_record[i].size()-1);
        phasing_record[i] = phasing_record[i].substr(0, phasing_record[i].size()-1);

	}
	match_record += "\t" + vcf_record[0] + "\t" + vcf_record[1];
    match_record += "\t" + phasing_record[0] + "\t" + phasing_record[1];
	match_record += "\t" + to_string(best_path.score) + "\n";

	complex_match_records[thread_index]->push_back(match_record);

    complex_ref_match_num[thread_index] += truth_num;
    complex_que_match_num[thread_index] += predict_num;
}


void WholeGenome::ConstructMatchRecordNoGenotype(SequencePath & best_path,
                                                 vector<DiploidVariant> & variant_list,
                                                 string & subsequence,
                                                 int offset,
                                                 int thread_index,
                                                 int chr_id){
    int truth_num = 0;
    int predict_num = 0;
    bool multiple_match = false;
    string parsimonious_ref = subsequence;
    string parsimonious_alt0 = best_path.donor_sequences[0];
    string parsimonious_alt1 = best_path.donor_sequences[0];

    int parsimonious_pos = NormalizeVariantSequence(offset,
                             parsimonious_ref,
                             parsimonious_alt0,
                             parsimonious_alt1,
                             chr_id);

    string match_record = to_string(parsimonious_pos+1) + "\t" + parsimonious_ref + "\t" + parsimonious_alt0;
    //if(multiple_match) match_record += "/" + parsimonious_alt1;

    string vcf_record[2];
    string phasing_record[2];

	for (int i = 0; i < 2; i++) {
		for (auto it = best_path.choice_made[i].begin(); it != best_path.choice_made[i].end(); ++it) {
            pair<int, int> selection = it->second;
            int phasing = selection.second;
            if(selection.first == -1) continue;
            if (phasing == -1) continue;
            DiploidVariant variant = variant_list[selection.first];
            if(variant.flag == 0){
                truth_num++;
            }else{
                predict_num++;
            }
            string alt_string = variant.alts[0];
            if(variant.multi_alts){
                alt_string += "/" + variant.alts[1];
            }
            string phasing_string = "";
            if(phasing == 0){
                phasing_string += "1|1";
            }else if(phasing == 1){
                phasing_string += "2|2";
            }
            string variant_record = to_string(variant.pos+1) + "," + variant.ref + "," + alt_string;
            vcf_record[i] += variant_record;
            phasing_record[i] += phasing_string;
            vcf_record[i] += ";";
            phasing_record[i] += ";";
		}
        vcf_record[i] = vcf_record[i].substr(0, vcf_record[i].size()-1);
        phasing_record[i] = phasing_record[i].substr(0, phasing_record[i].size()-1);

	}
	match_record += "\t" + vcf_record[0] + "\t" + vcf_record[1];
    match_record += "\t" + phasing_record[0] + "\t" + phasing_record[1];
	match_record += "\t" + to_string(best_path.score) + "\n";

	complex_match_records[thread_index]->push_back(match_record);

    complex_ref_match_num[thread_index] += truth_num;
    complex_que_match_num[thread_index] += predict_num;
}

bool WholeGenome::DonorLengthEqual(SequencePath & a, SequencePath & b){
    bool truth_same = false;
    bool query_same = false;

    if(a.donor_sequences[0].length() == b.donor_sequences[0].length() &&
       a.donor_sequences[1].length() == b.donor_sequences[1].length()){
        truth_same = true;
    }
    else if(a.donor_sequences[0].length() == b.donor_sequences[1].length() &&
            a.donor_sequences[1].length() == b.donor_sequences[0].length()){
                truth_same = true;
            }


    if(a.donor_sequences[2].length() == b.donor_sequences[2].length() &&
       a.donor_sequences[3].length() == b.donor_sequences[3].length()){
        query_same = true;
    }
    else if(a.donor_sequences[2].length() == b.donor_sequences[3].length() &&
            a.donor_sequences[3].length() == b.donor_sequences[2].length()){
                query_same = true;
            }

    if(truth_same && query_same) return true;
    return false;
}

bool IsRemovable(SequencePath & s){ return s.removable;}

void WholeGenome::ConvergePaths(list<SequencePath> & path_list){
    //dout << "===========start converge===================" << endl;
    int path_num = path_list.size();
    if(path_num <= 1) return;
    for(list<SequencePath>::iterator i = path_list.begin(); i!= path_list.end(); ++i){
        SequencePath  ref_path = *i;
        if(ref_path.removable) continue;
        if(!ref_path.same_donor_len) continue;
        list<SequencePath>::iterator j = i;
        ++j;
        for(; j != path_list.end(); ++j){
            SequencePath que_path = *j;
            if(que_path.removable) continue;
            if(!que_path.same_donor_len) continue;
            //dout << "Comparing following paths: " << endl;
            //PrintPath(ref_path);
            //PrintPath(que_path);
            if(DonorLengthEqual(ref_path, que_path)){
                if(ref_path.score >= que_path.score){
                    (*j).removable = true;
                    //dout << "delete path: " << endl;
                    //PrintPath((*j));
                }else{
                    (*i).removable = true;
                    //dout << "delete path: " << endl;
                    //PrintPath((*i));
                    break;
                }
            }
            //dout << "-    -     -   -   -   -   -  - - -" << endl;
        }
    }

    path_list.remove_if(IsRemovable);
}

int WholeGenome::test() {
	genome_sequences[0] = "GTCAGCCGG";
	DiploidVariant d1(1, "T", vector<string> ({"A", "C"}), true, true, 0,0,0);
	DiploidVariant d2(4, "G", vector<string> ({"C", ""}), true, false, 0,0,0);
	DiploidVariant d3(5, "C", vector<string> ({"T", ""}), true, false, 0,0,0); // this is false negative
	DiploidVariant d4(6, "C", vector<string> ({"G", ""}), true, false, 0,0,0);
	DiploidVariant d5(7, "G", vector<string> ({"A", ""}), true, false, 0,0,0);
	DiploidVariant d6(1, "T", vector<string> ({"A", "C"}), true, true, 0,0,1);
	DiploidVariant d7(3, "AG", vector<string> ({"A", ""}), true, false, 1,0,1);
	DiploidVariant d8(7, "G", vector<string> ({"GA", ""}), true, false, 0,1,1);

    complex_ref_match_num.push_back(0);
    complex_que_match_num.push_back(0);
    complex_match_records = new vector<string>*[1];
    complex_match_records[0] = new vector<string>;
	//vector<DiploidVariant> var_list = { d2,d3,d4,d5,d7,d8 };
	vector<DiploidVariant> var_list = { d1,d2,d3,d4,d5,d6,d7,d8 };
	//cout << MatchingSingleClusterBaseExtending(var_list, 0) << endl;
	cout << complex_match_records[0]->at(0) << endl;
	cout << complex_ref_match_num[0] << endl;
	cout << complex_que_match_num[0] << endl;
	return 0;
}

// private
void WholeGenome::ClusteringMatchMultiThread() {
	int start = 0;
	int cluster_number = variants_by_cluster.size(); // cluster number
	int cluster_end_boundary = start + cluster_number; // end cluster id, exclusive
	int cluster_step = cluster_number / thread_num; // assign clusters to threads
	if (cluster_step * thread_num < cluster_number) cluster_step++;
	int end = start + cluster_step;
	//initialize vector size
	complex_match_records = new vector<string>*[thread_num];
	for (int j = 0; j < thread_num; j++) {
		complex_match_records[j] = new vector<string>;
		complex_ref_match_num.push_back(0);
		complex_que_match_num.push_back(0);
	}

	vector<thread> threads;
	//spawn threads
	unsigned i = 0;
	for (; i < thread_num - 1; i++) {
		threads.push_back(thread(&WholeGenome::ClusteringMatchInThread, this, start, end, i));
		start = end;
		end = start + cluster_step;
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
	if (start >= variants_by_cluster.size()) {
		dout << "[Error] index out of map range" << endl;
	}
	else {
		ClusteringMatchInThread(start, end, i);
	}

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));

	ofstream output_complex_file;
	output_complex_file.open(output_complex_filename);
	output_complex_file << "##VCF1:" << ref_vcf_filename << endl;
	output_complex_file << "##VCF2:" << que_vcf_filename << endl;
	output_complex_file << "#CHROM\tPOS\tREF\tALT\tVCF1\tVCF2\tPHASE1\tPHASE2\tSCORE" << endl;
	for (int i = 0; i < thread_num; i++) {
		for (int j = 0; j < complex_match_records[i]->size(); j++) {
			if (complex_match_records[i]->at(j).find_first_not_of(' ') != std::string::npos) {
				output_complex_file << chromosome_name << "\t" << complex_match_records[i]->at(j);
			}
		}
	}
	output_complex_file.close();

	for (int j = 0; j < thread_num; j++) {
		delete complex_match_records[j];
	}
	delete[] complex_match_records;

	total_ref_complex = 0;
	total_que_complex = 0;
	for (int i = 0; i < complex_ref_match_num.size(); i++)
		total_ref_complex += complex_ref_match_num[i];
	for (int i = 0; i < complex_que_match_num.size(); i++)
		total_que_complex += complex_que_match_num[i];

	cout << "complex match: " << total_ref_complex << "," << total_que_complex << endl;
}


bool WholeGenome::MatchingSingleCluster(int cluster_index, int thread_index){

    vector<VariantIndicator> vi_list = variants_by_cluster[cluster_index];
    if(vi_list.size() <= 1) return false;
    // create variant_list from vi_list;
    vector<DiploidVariant> variant_list;
    int chr_id = -1;
    for(int i = 0; i < vi_list.size(); i++){
        VariantIndicator vi = vi_list[i];
        chr_id = vi.chr_id;
        int var_id = vi.var_id;
        if(vi.refer){
            variant_list.push_back(ref_variant_by_chrid[chr_id]->at(var_id));
        }else{
            variant_list.push_back(que_variant_by_chrid[chr_id]->at(var_id));
        }
    }
    if(chr_id == -1 || chr_id >= chrom_num){
        cout << "[VarMatch] Error in matching single cluster" << endl;
        return false;
    }


    // after this, all are copied from diploid.cpp
    // Warning: remember to change all genome_sequence to genome_sequences[chr_id]
    // Please!! use search!!


    //*****************************************************************************************************************
    //*****************************************************************************************************************
    //*****************************************************************************************************************
    //*****************************************************************************************************************
    //*****************************************************************************************************************


    sort(variant_list.begin(), variant_list.end()); // here we need to sort
    vector<DiploidVariant> separate_var_list[2];
	// separate into ref and que
	int total_mil = 0;
	int total_mdl = 0;
	int min_pos = genome_sequences[chr_id].length() + 1;
	int max_pos = -1;
	for (int i = 0; i < variant_list.size(); i++) {
		int flag = variant_list[i].flag; // flag indicate if the variant is from ref set(0) or query set(1)
		int pos = variant_list[i].pos;
		separate_var_list[flag].push_back(variant_list[i]);
		total_mil += variant_list[i].mil;
		total_mdl += variant_list[i].mdl;
		auto ref_sequence = variant_list[i].ref;
		auto alt_sequences = variant_list[i].alts;
		min_pos = min(pos, min_pos);
		max_pos = max((int)(pos + ref_sequence.length()), max_pos);
	}
	min_pos = max(min_pos - 1, 0);
	max_pos = min(max_pos + 1, (int)genome_sequences[chr_id].length()); //exclusive
	if (separate_var_list[0].size() == 0 || separate_var_list[1].size() == 0) {
		return false;
	}
	if (separate_var_list[0].size() == 1 && separate_var_list[1].size() == 1){
        // try direct match to save time
        if(separate_var_list[0][0] == separate_var_list[1][0]){
            complex_ref_match_num[thread_index]++;
            complex_que_match_num[thread_index]++;

            DiploidVariant tv = separate_var_list[0][0];
            string match_record = to_string(tv.pos+1) + "\t" + tv.ref + "\t" + tv.alts[0];
            if(tv.multi_alts) match_record += "/" + tv.alts[1];
            match_record += "\t.\t.\t.\t.\t.\n";
            complex_match_records[thread_index]->push_back(match_record);
            // output match result
            return true;
        }
        // if not match, still can match by changing genome
	}else if(separate_var_list[0].size() == 1 || separate_var_list[1].size() == 1){
        int flag = 0;
        if(separate_var_list[1].size() == 1) flag = 1;
        int r_flag = 1-flag;
        if(separate_var_list[r_flag].size() > 4){
            int total_r_mdl = 0;
            int total_r_mil = 0;

            for(int k = 0; k < separate_var_list[r_flag].size(); k++){
                DiploidVariant var = separate_var_list[r_flag][k];
                int var_mdl = var.mdl;
                int var_mil = var.mil;
                int ref_length = var.ref.length();
                total_r_mdl += var_mdl;
                total_r_mil += var_mil;
            }

            if(max(separate_var_list[flag][0].mdl, separate_var_list[flag][0].mil) > max(total_r_mdl, total_r_mil)) return false;
        }
	}

	// remove singular variant
    vector<bool> appliable_flag[2];
    int total_change = total_mil+total_mdl;
    for(int i = 0; i < 2; i++){
        for(int k = 0; k < separate_var_list[i].size(); k++){
            DiploidVariant cur_var = separate_var_list[i][k];
            int max_change = max(cur_var.mil, cur_var.mdl);
            if(max_change > total_change-max_change){
                appliable_flag[i].push_back(false);
            }else{
                appliable_flag[i].push_back(true);
            }
        }
    }
	string subsequence = genome_sequences[chr_id].substr(min_pos, max_pos - min_pos);

	ToUpper(subsequence); // subsequence only contains upper char
	int offset = min_pos;
	int subsequence_length = max_pos - min_pos;
	list<VariantSelection> variant_selections; // sorted by last matched donor length
	VariantSelection best_selection;
	VariantSelection dummy;

    bool overlap_detected = false;

    for(int i = 0; i < 2; i++){
        int largest_pos = 0;
        for(int k = 0; k < separate_var_list[i].size(); k++){
            auto var = separate_var_list[i][k];
            if(var.pos <= largest_pos){
                overlap_detected = true;
                break;
            }
            largest_pos = max(largest_pos, (int)(var.pos+var.ref.length()));
        }
        if(overlap_detected) break;
    }
    dummy.overlap_detected = overlap_detected;

    variant_selections.push_back(dummy);

    map<string, int> score_by_consistent_donor; // donor should be sorted

    while(variant_selections.size() != 0){
        VariantSelection current_selection = variant_selections.front();
        variant_selections.pop_front();

        bool get_ref_var = true;
        int ref_var_taken = current_selection.phasing_vectors[0].size();
        int que_var_taken = current_selection.phasing_vectors[1].size();
        if(ref_var_taken >= separate_var_list[0].size()){
            get_ref_var = false;
        }else if(que_var_taken < separate_var_list[1].size()){
              if(current_selection.genome_position[0] > current_selection.genome_position[1]){
                get_ref_var = false;
              }else if( current_selection.genome_position[0] == current_selection.genome_position[1]){
                if(min(current_selection.donor_length[0], current_selection.donor_length[1]) > min(current_selection.donor_length[2], current_selection.donor_length[3])){
                    get_ref_var = false;
                }
              }
        }

        DiploidVariant current_variant;
        bool can_take_variant = true;
        if(get_ref_var){
            can_take_variant = appliable_flag[0][ref_var_taken];
            current_variant = separate_var_list[0][ref_var_taken];
        }else{
            can_take_variant = appliable_flag[1][que_var_taken];
            current_variant = separate_var_list[1][que_var_taken];
        }

        int current_flag = current_variant.flag;

//            cout << "current selection" << endl;
//            PrintSelection(current_selection);
//            cout << "add variant";
//            PrintVariant(current_variant);

        bool added = false;
        // make choose decision before not choose decision, save del times
        if(can_take_variant){
            added = AddVariantToSelection(variant_selections,
                                current_selection,
                                current_variant,
                                0,
                                separate_var_list,
                                subsequence,
                                offset,
                                best_selection);
    //            cout << "added state : " << added << endl;
    //            PrintSelectionsList(variant_selections);

            if(current_variant.heterozygous){
                added = AddVariantToSelection(variant_selections,
                                    current_selection,
                                    current_variant,
                                    1,
                                    separate_var_list,
                                    subsequence,
                                    offset,
                                    best_selection);
    //                cout << "added state : " << added << endl;
    //                PrintSelectionsList(variant_selections);
            }
        }

       added= AddVariantToSelection(variant_selections,
                            current_selection,
                            current_variant,
                            -1,
                            separate_var_list,
                            subsequence,
                            offset,
                            best_selection);
//            cout << "added state : " << added << endl;
//            PrintSelectionsList(variant_selections);

    }
//    dout << best_selection.score << endl;
    if (best_selection.score <= 0) return false;
//    cout << "best selection: " << endl;
//    PrintSelection(best_selection);
    //dout << "cluster id: " << cluster_index << "| old method : " << best_selection.score << "," ;

    complex_ref_match_num[thread_index] += best_selection.separate_score[0];
    complex_que_match_num[thread_index] += best_selection.separate_score[1];

    bool multiple_match = true;



    if(best_selection.donor_sequences[0] == best_selection.donor_sequences[1]) multiple_match = true;


    string parsimonious_ref = subsequence;
    string parsimonious_alt0 = best_selection.donor_sequences[0];
    string parsimonious_alt1 = best_selection.donor_sequences[1];

    int parsimonious_pos = NormalizeVariantSequence(offset,
                             parsimonious_ref,
                             parsimonious_alt0,
                             parsimonious_alt1,
                             chr_id);

    string match_record = to_string(parsimonious_pos+1) + "\t" + parsimonious_ref + "\t" + parsimonious_alt0;
    if(multiple_match) match_record += "/" + parsimonious_alt1;

    string vcf_record[2];
    string phasing_record[2];

	for (int i = 0; i < 2; i++) {
		auto final_iter = separate_var_list[i].size()-1;
		vector<int> phasing_vector = best_selection.phasing_vectors[i];
		for (int k = 0; k < separate_var_list[i].size(); k++) {
            int phasing = phasing_vector[k];
            if(phasing == -1) continue;
            DiploidVariant variant = separate_var_list[i][k];
            string alt_string = variant.alts[0];
            if(variant.multi_alts){
                alt_string += "/" + variant.alts[1];
            }
            string phasing_string = "";
            if(phasing == 0){
                phasing_string += "1";
                if(variant.heterozygous){
                    if(variant.multi_alts){
                        phasing_string += "|2";
                    }else{
                        phasing_string += "|0";
                    }
                }else{
                    phasing_string += "|1";
                }
            }else if(phasing == 1){
                if(variant.multi_alts){
                    phasing_string += "2|1";
                }else{
                    phasing_string += "0|1";
                }
            }
            string variant_record = to_string(variant.pos+1) + "," + variant.ref + "," + alt_string;
            vcf_record[i] += variant_record;
            phasing_record[i] += phasing_string;
            if (k != final_iter) {
                vcf_record[i] += ";";
                phasing_record[i] += ";";
            }
		}
	}
	match_record += "\t" + vcf_record[0] + "\t" + vcf_record[1];
    match_record += "\t" + phasing_record[0] + "\t" + phasing_record[1];
	match_record += "\t" + to_string(best_selection.score) + "\n";

	complex_match_records[thread_index]->push_back(match_record);
    // add matching result
    return true;
}

int WholeGenome::NormalizeVariantSequence(int pos, string & parsimonious_ref, string & parsimonious_alt0, string & parsimonious_alt1, int chr_id) {

	int left_index = pos;
	if (genome_sequences[chr_id].size() == 0) return -1;
	if (parsimonious_ref.size() == 1 && parsimonious_alt0.size() == 1 && parsimonious_alt1.size() == 1) return true;

	bool change_in_allels = true;
	while (change_in_allels) {
		change_in_allels = false;
		if (parsimonious_ref.back() == parsimonious_alt0.back() && parsimonious_ref.back() == parsimonious_alt1.back() ) {
			if ((parsimonious_ref.size() > 1 && parsimonious_alt0.size() > 1 && parsimonious_alt1.size() > 1) || left_index > 0) { // when left_index == 0, can not make further changes
				parsimonious_ref.pop_back();
				parsimonious_alt0.pop_back();
				parsimonious_alt1.pop_back();
				change_in_allels = true;
			}
            // else do not make further changes
		}
		if (parsimonious_ref.length() == 0 || parsimonious_alt0.length() == 0 || parsimonious_alt1.length() == 0) {
			left_index--;
			char left_char = toupper(genome_sequences[chr_id][left_index]);
			parsimonious_ref = left_char + parsimonious_ref;
			parsimonious_alt0 = left_char + parsimonious_alt0;
			parsimonious_alt1 = left_char + parsimonious_alt1;
		}
	}
	while (parsimonious_ref[0] == parsimonious_alt0[0] &&
            parsimonious_ref[0] == parsimonious_alt1[0] &&
            parsimonious_ref.size() > 1 &&
            parsimonious_alt0.size() > 1 &&
            parsimonious_alt1.size() > 1)
    {
		parsimonious_ref.erase(0, 1);
		parsimonious_alt0.erase(0, 1);
		parsimonious_alt1.erase(0, 1);
        left_index ++; // left_index indicates variant position, if truncate the leftmost, then
	}
	return left_index;
}

void WholeGenome::SingleThreadClustering(int chr_id) {
	int ins_len[2] = { 0 };
	int del_len[2] = { 0 };
	int c_start = 0;
	int c_end = 0;
    sort(ref_variant_by_chrid[chr_id]->begin(), ref_variant_by_chrid[chr_id]->end());
    sort(que_variant_by_chrid[chr_id]->begin(), que_variant_by_chrid[chr_id]->end());
    int ref_size = ref_variant_by_chrid[chr_id]->size();
    int que_size = que_variant_by_chrid[chr_id]->size();
    //dout << chr_id << "," << ref_size << "," << que_size << endl;

    int ref_index = 0;
    int que_index = 0;
    bool not_first = false;
    DiploidVariant snp;
    vector<VariantIndicator> vi_list;
    while (ref_index < ref_size || que_index < que_size) {
		bool take_que = true;
		if(ref_index < ref_size && que_index < que_size){
            if(ref_variant_by_chrid[chr_id]->at(ref_index).pos < que_variant_by_chrid[chr_id]->at(que_index).pos){
                take_que = false;
            }
		}else if(ref_index < ref_size){
            take_que = false;
		}
        int var_index;
		if(take_que){

            snp = que_variant_by_chrid[chr_id]->at(que_index);
            //cout << "q |" << que_index << "," << snp.pos << endl;
            var_index = que_index;
            que_index++;
		}else{
            snp = ref_variant_by_chrid[chr_id]->at(ref_index);
            //cout << "r |" << ref_index << "," << snp.pos << endl;
            var_index = ref_index;
            ref_index++;
		}
		// check if need to separator clusters
		if (not_first) {
			c_end = snp.pos;
			if (c_end - c_start >= 2) {
                int separator_length = c_end - c_start;
				string separator = genome_sequences[chr_id].substr(c_start, separator_length);
				int max_change = max(ins_len[0] + del_len[1], ins_len[1] + del_len[0]);
				bool separate_cluster = false;
				if(max_change == 0){
                    separate_cluster = true;
				}
				else if (separator_length > 2 * max_change &&
					(separator_length > MAX_REPEAT_LEN || !CheckTandemRepeat(separator, max_change)))
				{
				    separate_cluster = true;
				}

				if(separate_cluster){
                    variant_cluster_by_chrid[chr_id]->push_back(vi_list);
                    vi_list.clear();
					ins_len[0] = 0;
					del_len[0] = 0;
					ins_len[1] = 0;
					del_len[1] = 0;
					c_start = 0; // re-assign c_start
				}
			}
		}
		c_start = max(c_start, snp.pos + (int)snp.ref.length() );
        VariantIndicator current_variant_indicator(chr_id, var_index, !take_que);
        vi_list.push_back(current_variant_indicator);
		//cluster_vars_map[cluster_index].push_back(snp);
		if(!not_first) not_first = true;
		int ref_length = (int)(snp.ref.length());
		int flag = snp.flag;
//        DiploidVariant snp = front_cluster[k];
//        int rq = snp.flag;
        ins_len[flag] += snp.mil;
        del_len[flag] += snp.mdl;
	}
}

int WholeGenome::ReadReferenceVariants(string filename){
    return ReadWholeGenomeVariant(filename, 0);
}

int WholeGenome::ReadQueryVariants(string filename){
    return ReadWholeGenomeVariant(filename, 1);
}

void WholeGenome::Compare(string ref_vcf,
	string query_vcf,
	string genome_seq,
	string output_prefix)
{
    if(score_scheme == 3){
        DirectMatch(ref_vcf, query_vcf);
        return;
    }
	ref_vcf_filename = ref_vcf;
	que_vcf_filename = query_vcf;
	this->normalization = false;
	this->variant_check = false;

	output_stat_filename = output_prefix + ".stat";
    output_complex_filename = output_prefix + ".match";

    ReadWholeGenomeSequence(genome_seq);
    int ref_variant_num = ReadReferenceVariants(ref_vcf);
    int que_variant_num = ReadQueryVariants(query_vcf);
    dout << ref_variant_num << "," << que_variant_num << endl;

    ParallelClustering();
    ClusteringMatchMultiThread();
}

void WholeGenome::DirectMatch(string ref_vcf, string query_vcf)
{

    int ref_variant_num = ReadReferenceVariants(ref_vcf);
    int que_variant_num = ReadQueryVariants(query_vcf);
    dout << ref_variant_num << "," << que_variant_num << endl;
    int match_num = 0;
    for(int i = 0; i < chrom_num; i++){
        if(ref_variant_by_chrid[i]->size() == 0 || que_variant_by_chrid[i]->size() == 0)
            continue;
        //[TODO] not the right way to do it, at least need multimap
        multimap<int, int> ref_variant_by_pos;
        for(int j = 0; j < ref_variant_by_chrid[i]->size(); j++){
            DiploidVariant var = ref_variant_by_chrid[i]->at(j);
            int pos = var.pos;
            ref_variant_by_pos.insert(pair<int, int>(pos, j));
        }

        for(int j = 0; j < que_variant_by_chrid[i]->size(); j++){
            DiploidVariant var = que_variant_by_chrid[i]->at(j);
            int pos = var.pos;
            if(ref_variant_by_pos.find(pos) == ref_variant_by_pos.end())
                continue;

            pair<multimap<int, int>::iterator, multimap<int, int>::iterator> var_range;
            var_range = ref_variant_by_pos.equal_range(pos);

            for(auto it = var_range.first; it != var_range.second; ++it){
                int ref_index = (*it).second;
                DiploidVariant ref_var = ref_variant_by_chrid[i]->at(ref_index);
                if (var == ref_var){
                    match_num ++;
                    break;
                }
            }
        }
    }
    dout << "matched variants: " << match_num << endl;
}























