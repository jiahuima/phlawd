/*
 * PHLAWD: Phylogeny assembly with databases
 * Copyright (C) 2010  Stephen A. Smith
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

/*
 * SQLiteConstructor.cpp
 */

//TODO: need to edit the reverse complement stuff in here

#include "Same_seq_pthread.h"
#include "SWPS3_Same_seq_pthread.h"
#include "SWPS3_matrix.h"

#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <stdlib.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <time.h>
#include <math.h>

using namespace std;

#include "libsqlitewrapped.h"


#include "sequence.h"
#include "fasta_util.h"

#include "SQLiteConstructor.h"
#include "DBSeq.h"
#include "utils.h"

//public

template <class T>
inline std::string to_string (const T& t)
{
std::stringstream ss;
ss << t;
return ss.str();
}

SQLiteConstructor::SQLiteConstructor(string cn, vector <string> searchstr, string genen,
		double mad_cut,double cover, double ident, string dbs, string known_seq_filen, bool its, int numt,bool autom){
	clade_name=cn;
	search=searchstr;
	gene_name = genen;
	mad_cutoff = mad_cut;
	coverage = cover;
	identity=ident;
	db = dbs;
	useITS = its;
	numthreads = numt;
	automated = autom;
	FastaUtil seqReader;
	known_seqs = new vector<Sequence>();
	seqReader.readFile(known_seq_filen, *known_seqs);
}

void SQLiteConstructor::set_only_names_from_file(string filename,bool containshi){
	onlynamesfromfile = true;
	containshigher = containshi;
	listfilename = filename;
}

void SQLiteConstructor::set_exclude_names_from_file(string filename){
	excludenamesfromfile = true;
	excludefilename = filename;
}

void SQLiteConstructor::set_exclude_gi_from_file(string filename){
	excludegifromfile = true;
	exclude_gi_filename = filename;
}

void SQLiteConstructor::run(){
	string logn = gene_name;
	logn.append(".log");
	logfile.open(logn.c_str());
	string gin = gene_name;
	gin.append(".gi");
	gifile.open(gin.c_str());
	//gifile << "tax_id\tncbi_tax_id\tgi_number" << endl;
	gifile << "ncbi_tax_id\tgi_number" << endl;

	// if temp directory doesn't exist
	mkdir("TEMPFILES",S_IRWXU | S_IRWXG | S_IROTH | S_IWOTH);
	mkdir(gene_name.c_str(),S_IRWXU | S_IRWXG | S_IROTH | S_IWOTH);

	vector<vector<string> > start_res;
	first_seq_search_for_gene_left_right(start_res);

	//make connection to database
	Database conn(db);
	cout << "connected to " << db << endl;
	vector<int> R;
	if (automated == false){
		Query query(conn);
		query.get_result("SELECT ncbi_id FROM taxonomy WHERE name = '"+clade_name+"'");
		while(query.fetch_row()){
			R.push_back(query.getval());
		}
		query.free_result();
	}else if(automated == true){
		Query query(conn);
		query.get_result("SELECT ncbi_id FROM taxonomy WHERE ncbi_id = "+clade_name);
		while(query.fetch_row()){
			R.push_back(query.getval());
		}
		query.free_result();
	}
	//start with a name -- get the broad name clade id
	cout << "Found " << R.size()<< " taxon ids:" << endl;
	for(int i = 0; i < R.size(); ++i){
		cout << R[i] << endl;
	}
	int name_id;
	//string sname_id;
	name_id = R[0];
	string sname_id = to_string(R[0]).c_str();
	cout << "Will be using " << name_id << endl;
	//name_id = 15719;
	//sname_id = "15719"
	//cout << "OVERRIDE!!! using: " << name_id << endl;

	//start with a set of seqs given the first clade name and the regions
	vector<DBSeq> startseqs = first_get_seqs_for_name_use_left_right(name_id, start_res);

	cout << "first: " << startseqs.size() << endl;

	//if only using the names from a list
	if(onlynamesfromfile == true){
		startseqs = use_only_names_from_file(startseqs);
		cout << "after names: " << startseqs.size() << endl;
	}

	//if excluding names from file

	if(excludenamesfromfile == true){
		startseqs = exclude_names_from_file(startseqs);
		cout << "after excluding names: " << startseqs.size() << endl;
	}

	//if excluding gi's from file
	if(excludegifromfile == true){
		startseqs = exclude_gis_from_file(startseqs);
		cout << "after excluding gis: " << startseqs.size() << endl;
	}

	//use blast to idenify seqs and rc
	vector<DBSeq> * keep_seqs = new vector<DBSeq>();
	vector<bool> * keep_rc = new vector<bool>();

	/*
	 * not sure where ITS should go, but maybe before blasting
	 * then the blasting statistics can be strict
	 */

	if(useITS == true){
		cout << "starting ITS mode" <<endl;
		combine_ITS(&startseqs);
		cout << "after ITS " << startseqs.size() << endl;
	}

	//get_same_seqs(startseqs, keep_seqs, keep_rc);
	//get_same_seqs_pthreads(startseqs, keep_seqs, keep_rc);

	get_same_seqs_pthreads_SWPS3(startseqs, keep_seqs, keep_rc);
	cout << "blasted: "<< keep_seqs->size() << endl;
	for(int i=0;i<keep_rc->size();i++){
			cout << keep_rc->at(i);
	}cout << endl;

	//remove duplicate names
	//remove_duplicates(keep_seqs, keep_rc);
	remove_duplicates_SWPS3(keep_seqs, keep_rc);
	cout << "dups: "<< keep_seqs->size() << endl;
	for(int i=0;i<keep_rc->size();i++){
		cout << keep_rc->at(i);
	}cout << endl;

	/*
	 * reduce genome sequences
	 */
	reduce_genomes(keep_seqs, keep_rc);
	
	//saturation tests
	saturation_tests(sname_id, keep_seqs, keep_rc);

	logfile.close();
	gifile.close();
	delete known_seqs;
	delete keep_seqs;
	delete keep_rc;
}

string SQLiteConstructor::get_cladename(){
	return clade_name;
}

vector <string> SQLiteConstructor::get_search(){
	return search;
}

string SQLiteConstructor::get_genename(){
	return gene_name;
}

double SQLiteConstructor::get_madcutoff(){
	return mad_cutoff;
}

double SQLiteConstructor::get_coverage(){
	return coverage;
}

double SQLiteConstructor::get_identity(){
	return identity;
}

int SQLiteConstructor::get_numthreads(){
	return numthreads;
}

//private
/*
 * should retrieve all the matches for a sequence based on the description
 * should return the vector of two strings of the ids,taxon_ids in the sequence table
 */
void SQLiteConstructor::first_seq_search_for_gene_left_right(vector<vector<string> > & retvals){
	Database conn(db);
	string sql;
	if (search.size() == 1){
		sql = "SELECT id,ncbi_id FROM sequence WHERE description LIKE '%"+search[0]+"%'";
	}else{
		sql = "SELECT id,ncbi_id FROM sequence WHERE ";
		for(int i=0;i<search.size()-1;i++){
			sql = sql + "description LIKE '%"+search[i]+"%' OR ";
		}
		sql = sql + "description LIKE '%"+search[search.size()-1]+"%';";
	}
	//string sql = "SELECT * FROM bioentry WHERE description LIKE '%"+search+"%'";
//	sql = "SELECT * FROM bioentry WHERE description LIKE '%"+search+"%' OR description LIKE '%trnK%'"
	Query query(conn);
	query.get_result(sql);
	int co = 1;
	while(query.fetch_row()){
		vector<string> vals;
		vals.push_back(to_string(query.getval()));
		vals.push_back(to_string(query.getval()));
		retvals.push_back(vals);
		co += 1;
	}
	cout << "search number " << co << endl;
	query.free_result();
}

vector<DBSeq> SQLiteConstructor::first_get_seqs_for_name_use_left_right
									(int name_id, vector<vector<string> > & results){
	Database conn(db);
	string deeptaxid;
	deeptaxid = to_string(name_id);
	string sql = "SELECT left_value,right_value FROM taxonomy WHERE ncbi_id = "+deeptaxid;
	Query query(conn);
	query.get_result(sql);
	int left_value_name;
	int right_value_name;
	while(query.fetch_row()){
		left_value_name = query.getval();
		right_value_name = query.getval();
	}
	query.free_result();
	vector <DBSeq> seqs;
	int left_value, right_value,ncbi_id;
	string bioentid;
	for (int i =0 ; i < results.size(); i++){
		string ncbi = results[i][1];
		string taxid = "";
		sql = "SELECT id,left_value,right_value FROM taxonomy WHERE ncbi_id = "+ncbi+" and name_class = 'scientific name';";
		Query query2(conn);
		query2.get_result(sql);
		while(query2.fetch_row()){
			taxid = query2.getval();
			left_value = query2.getval();
			right_value = query2.getval();
		}
/*
		StoreQueryResult temp1R = query2.store();
		try{
			left_value = temp1R[0][6];
			right_value = temp1R[0][7];
		}catch(mysqlpp::BadConversion E){
			cout << "some sort of taxa [left/right] error (SQLiteConstructor line:267 -- taxon.taxon_id " << taxid <<")" << endl;
		}
*/
		query2.free_result();
		if (left_value > left_value_name && right_value < right_value_name){
			bioentid = results[i][0];
			sql = "SELECT accession_id,identifier,description,seq FROM sequence WHERE id = "+bioentid;
			Query query3(conn);
			query3.get_result(sql);
			string descr,acc,gi,sequ;
			while(query3.fetch_row()){
				acc = query3.getstr();
				gi = query3.getstr();
				descr = query3.getstr();
				sequ = query3.getstr();
			}
			query3.free_result();
			DBSeq tseq(ncbi, sequ, acc, gi,ncbi, taxid, descr);
			seqs.push_back(tseq);
		}
	}
	return seqs;
}

vector<DBSeq> SQLiteConstructor::use_only_names_from_file(vector<DBSeq> seqs){
	Database conn(db);
	vector<string> * taxa =new vector<string>();
	vector<string> * taxa_ids =new vector<string>();
	//read file
	ifstream ifs(listfilename.c_str());
	string line;
	while(getline(ifs,line)){
		TrimSpaces(line);
		taxa->push_back(line);
		string sql = "SELECT ncbi_id FROM taxonomy WHERE name = '"+line+"'";
		Query query1(conn);
		query1.get_result(sql);
		while(query1.fetch_row()){
			int id;
			id = query1.getval();
			taxa_ids->push_back(to_string(id));
		}
		query1.free_result();
	}
	cout << taxa_ids->size() << " names in the file" << endl;
	ifs.close();
	//end read file
	vector<DBSeq> seqs_fn;
	for(int i=0;i<seqs.size();i++){
		//string taxid = seqs[i].get_tax_id();
		string taxid = seqs[i].get_ncbi_taxid();
		int scount = count(taxa_ids->begin(),taxa_ids->end(),taxid);
		if(scount > 0){
			seqs_fn.push_back(seqs[i]);
		}
	}
	//print len(seqs),len(seqs_fn)
	//for seq in seqs_fn:
	//	seqs.remove(seq)
	//print len(seqs),len(seqs_fn)
	/*
	 * added for higher taxa
	 */
	if(containshigher == true){
		cout << "this file contains higher taxa" << endl;
		for(int i=0;i < taxa_ids->size();i++){
			string sql = "SELECT ncbi_id FROM taxonomy WHERE parent_ncbi_id = "+taxa_ids->at(i)+" and name_class = 'scientific name';";
			Query query(conn);
			query.get_result(sql);
			long count = query.num_rows();
			cout << count << endl;
			if(count == 0){
				continue;
			}else{
				try{
					DBSeq tse = add_higher_taxa(taxa_ids->at(i),seqs);
					seqs_fn.push_back(tse);
				}catch(int a){

				}
			}
			query.free_result();
		}
	}
	/*
	 * end added higher taxa
	 */
	delete taxa;
	delete taxa_ids;
	return seqs_fn;
}

/*
 * called from use_only_names_from_file,  includes the procedure
 * to deal with higher taxa -- this does a mini phlawd construct
 * on the higher taxa to pick the best one
 * steps are
 * 1) send higher taxa name from use_only_names_from_file (which sends
 * based on a name in the list having children)
 * 2) get all seqs of the higher taxa from seqs (sent along)
 * 3) ortho check again known files
 * 4) take best seq (best overall)
 * 5) store in highertaxa container
 * 6) add these to those that pass the ortho check later (need to store
 * the seq in a file so that the higher taxa id is associated with the
 * smaller one (higher is in the phlawd file, smaller is store) saved
 * with the seq)
 */

DBSeq SQLiteConstructor::add_higher_taxa(string taxon_id,vector<DBSeq> seqs){
	vector<string> children_ids = get_final_children(taxon_id);
	//get all the seqs in the set that are within the higher taxon
	vector<DBSeq> seqs_fn2;
	for(int i=0;i<seqs.size();i++){
		//string taxid = seqs[i].get_tax_id();
		string taxid = seqs[i].get_ncbi_taxid();
		int scount = count(children_ids.begin(),children_ids.end(),taxid);
		if(scount > 0){
			seqs_fn2.push_back(seqs[i]);
		}
	}
	if (seqs_fn2.size() == 0){
		throw 0;
	}else{
		/*
		 * now get the best sequence in the set
		 */
		vector<DBSeq> * keep_seqs2 = new vector<DBSeq>();
		vector<bool> * keep_rc2 = new vector<bool>();
		get_same_seqs_pthreads_SWPS3(seqs_fn2,keep_seqs2,keep_rc2);
		//take keep_seqs and the known_seqs and get the distances and get the best
		vector<int> scores;
		SBMatrix mat = swps3_readSBMatrix( "EDNAFULL" );
		for(int i=0;i<known_seqs->size();i++){
			//TODO : there was a pointer problem here
			scores.push_back(get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(i),&known_seqs->at(i)));
		}

		int bestid = 0;
		double bestiden = 0;
		if(keep_seqs2->size() > 0){
			for (int i=0;i<keep_seqs2->size();i++){
				DBSeq tseq = keep_seqs2->at(i);
				double maxiden = 0;
				bool rc = false;
				for (int j=0;j<known_seqs->size();j++){
					bool trc = false;
					//TODO : there was a pointer problem here
					int ret = get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j), & tseq);
					double tsc = double(ret)/double(scores[j]);
					Sequence tseqrc;
					tseqrc.set_id(tseq.get_id());
					tseqrc.set_sequence(tseq.reverse_complement());
					//TODO : there was a pointer problem here
					int retrc = get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j), &tseqrc);
					if(retrc > ret){
						trc = true;
						tsc = double(retrc)/double(scores[j]);
					}
					if (tsc > maxiden){
						maxiden = tsc;
						rc = trc;
					}
					//cout << tsc << endl;
				}
				if (maxiden >= bestiden){
					bestid = i;
					bestiden = maxiden;
				}
			}
			DBSeq bestseq = keep_seqs2->at(bestid);
			bestseq.set_id(taxon_id); //name will be the higher taxon name
			cout << "higher taxa change" << endl;
			//cout << taxon_id << "=" << bestseq.get_tax_id() << endl;
			cout << taxon_id << "=" << bestseq.get_ncbi_taxid() << endl;
			logfile << "higher taxa change\n";
			//logfile << taxon_id << "=" << bestseq.get_tax_id() << "\n";
			logfile << "ncbi: " << bestseq.get_ncbi_taxid() << "\n";
			logfile << "descr: " << bestseq.get_descr() << "\n";
			delete keep_seqs2;
			delete keep_rc2;

			/*
			 * return the best sequence
			 */
			return bestseq;
		}else{
			throw 0;
		}
	}
}

/*
 * excluding taxa from sequences
 */
vector<DBSeq> SQLiteConstructor::exclude_names_from_file(vector<DBSeq> seqs){
	Database conn(db);
	vector<string> * taxa =new vector<string>();
	vector<string> * taxa_ids =new vector<string>();
	//read file
	ifstream ifs(excludefilename.c_str());
	string line;
	while(getline(ifs,line)){
		TrimSpaces(line);
		taxa->push_back(line);
		string sql = "SELECT ncbi_id FROM taxonomy WHERE name = '"+line+"'";
		Query query1(conn);
		query1.get_result(sql);
		while(query1.fetch_row()){
			int id;
			id = query1.getval();
			taxa_ids->push_back(to_string(id));
		}
		query1.free_result();
	}
	cout << taxa_ids->size() << " names in the file" << endl;
	ifs.close();
	//end read file
	vector<DBSeq> seqs_fn;
	for(int i=0;i<seqs.size();i++){
		//string taxid = seqs[i].get_tax_id();
		string taxid = seqs[i].get_ncbi_taxid();
		int scount = count(taxa_ids->begin(),taxa_ids->end(),taxid);
		if(scount == 0){
			seqs_fn.push_back(seqs[i]);
		}
	}
	delete taxa;
	delete taxa_ids;
	return seqs_fn;
}

/*
 * excluding gis from sequences
 */
vector<DBSeq> SQLiteConstructor::exclude_gis_from_file(vector<DBSeq> seqs){
	vector<string> * gi_ids =new vector<string>();
	//read file
	ifstream ifs(exclude_gi_filename.c_str());
	string line;
	while(getline(ifs,line)){
		TrimSpaces(line);
		gi_ids->push_back(line);
	}
	cout << gi_ids->size() << " gis in the file" << endl;
	ifs.close();
	//end read file
	vector<DBSeq> seqs_fn;
	for(int i=0;i<seqs.size();i++){
		//string giid = seqs[i].get_accession();
		string giid = seqs[i].get_gi();
		int scount = count(gi_ids->begin(),gi_ids->end(),giid);
		if(scount == 0){
			seqs_fn.push_back(seqs[i]);
		}
	}
	delete gi_ids;
	return seqs_fn;
}


vector<double> SQLiteConstructor::get_blast_score_and_rc(Sequence inseq1, DBSeq inseq2, bool * rc){
	vector<double> retvalues;
	FastaUtil seqwriter1;
	FastaUtil seqwriter2;
	vector<Sequence> sc1;
	vector<Sequence> sc2;
	const string fn1 = "seq1";
	const string fn2 = "seq2";
	seqwriter1.writeFileFromVector(fn1,sc1);
	seqwriter2.writeFileFromVector(fn2,sc2);
//	string cmd = "bl2seq -i seq1 -j seq2 -p blastn -D 1";
	double coverage = 0;
	double identity = 0;
	string line;

	const char * cmd = "bl2seq -i seq1 -j seq2 -p blastn -D 1";
	FILE *fp = popen(cmd, "r" );
	char buff[1000];
	vector<string> tokens;
	while ( fgets( buff, sizeof buff, fp ) != NULL ) {//doesn't exit out
		string line(buff);

		size_t found=line.find("#");
		if (found==string::npos){
			//cout << "XXX " << line << endl;
			string del("\t");
			Tokenize(line, tokens, del);
			for (int i=0;i<tokens.size();i++){
				//cout << i << " " << tokens[i] << endl;
			}
			coverage = coverage + strtod(tokens[3].c_str(),NULL);
			if (strtod(tokens[2].c_str(),NULL) > identity){
				identity = strtod(tokens[2].c_str(),NULL);
			}
		}
		//cout << buff;
	}
	pclose( fp );
	if (tokens.size() < 1){
		return retvalues;
	}else{
		//bool rc = false;
		if (strtod(tokens[8].c_str(),NULL)>strtod(tokens[9].c_str(),NULL))
			*rc=true;
		else
			*rc=false;
		cout << *rc;
	}
	retvalues.push_back(identity/100.0);
	retvalues.push_back(coverage/(int)inseq1.get_sequence().size());
	return retvalues;
	//return (float(maxident/100.0),float(coverage/len(seq1.seq.tostring())),rc)
}

//vector< vector<DBSeq> >
void SQLiteConstructor::get_same_seqs(vector<DBSeq> seqs,  vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	//vector<DBSeq> keep_seqs;
	//vector<DBSeq> keep_rc;
	double maxide = 0;
	double maxcov = 0;
	bool rc = false;
	int reports = 100;
	for (int i=0;i<seqs.size();i++){
		if(i%reports == 0){
			cout << i << endl;
		}
		maxide = 0;
		maxcov = 0;
		rc = false;
		for (int j=0;j<known_seqs->size();j++){
			bool trc = false;
			//TODO : there was a pointer problem here
			vector<double> ret = get_blast_score_and_rc(known_seqs->at(j), seqs[i], &trc); //should be pointer?
			if (ret.size() > 1){
				/*if (ret[0] >maxide){
					maxide = ret[0];
				}
				if (ret[1] > maxcov){ // should these be in the same conditional statement
					maxcov = ret[1];
					rc = trc;//need to get it somewhere else -- pointer probably
				}*/
				if (ret[0] >maxide && ret[1] > maxcov){ // should these be in the same conditional statement
					maxide = ret[0];
					maxcov = ret[1];
					rc = trc;//need to get it somewhere else -- pointer probably
				}
			}
		}
		if (maxide >= identity && maxcov >= coverage){
			keep_seqs->push_back(seqs[i]);
			keep_rc->push_back(rc);
		}
	}
}


void SQLiteConstructor::get_same_seqs_pthreads(vector<DBSeq> seqs,  vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	//vector<DBSeq> keep_seqs;
	//vector<DBSeq> keep_rc;

	/*
	 * begin the parallelization here
	 */
	//split the seqs into the num of threads
	//vector<Same_seq_pthread_storage> storage;

	struct thread_data thread_data_array[numthreads];

	for (int i=0;i<numthreads; i++){
		vector <DBSeq> st_seqs;
		if((i+1) < numthreads){
			for(unsigned int j=(i*(seqs.size()/numthreads));j<((seqs.size()/numthreads))*(i+1);j++){
			//for(int j=(i*(seqs.size()/numthreads));j<100;j++){
				st_seqs.push_back(seqs[j]);
			}
		 }else{//last one
			for(unsigned int j=(i*(seqs.size()/numthreads));j<seqs.size();j++){
			//for(int j=(i*(seqs.size()/numthreads));j<100;j++){
				st_seqs.push_back(seqs[j]);
			}
		 }
		 cout << "spliting: " << st_seqs.size() << endl;
		//Same_seq_pthread_storage temp (st_seqs,coverage,identity);
		//storage.push_back(temp);
		thread_data_array[i].thread_id = i;
		thread_data_array[i].seqs = st_seqs;
		thread_data_array[i].coverage = coverage;
		thread_data_array[i].identity = identity;
		thread_data_array[i].reports = 100;
		thread_data_array[i].known_seqs = known_seqs;
		vector<DBSeq> keep_seqs1;
		vector<bool> keep_rc1;
		thread_data_array[i].keep_seqs = keep_seqs1;
		thread_data_array[i].keep_rc = keep_rc1;
	}
	pthread_t threads[numthreads];
	void *status;
	int rc;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for(int i=0; i <numthreads; i++){
		cout << "thread: " << i <<endl;
		rc = pthread_create(&threads[i], &attr, Same_seq_pthread_go, (void *) &thread_data_array[i]);
		if (rc){
			printf("ERROR; return code from pthread_create() is %d\n", rc);
			exit(-1);
		}
	}
	pthread_attr_destroy(&attr);
	for(int i=0;i<numthreads; i++){
		cout << "joining: " << i << endl;
		pthread_join( threads[i], &status);
		if (rc){
			printf("ERROR; return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
		printf("Completed join with thread %d status= %ld\n",i, (long)status);
	}
	/*
	 * bring em back and combine for keep_seqs and keep_rc
	 */
	for (int i=0;i<numthreads; i++){
		for(int j=0;j<thread_data_array[i].keep_seqs.size();j++){
			keep_seqs->push_back(thread_data_array[i].keep_seqs[j]);
			keep_rc->push_back(thread_data_array[i].keep_rc[j]);
		}

	}
}

void SQLiteConstructor::get_same_seqs_pthreads_SWPS3(vector<DBSeq> seqs,  vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	//vector<DBSeq> keep_seqs;
	//vector<DBSeq> keep_rc;

	/*
	 * begin the parallelization here
	 */
	//split the seqs into the num of threads
	//vector<Same_seq_pthread_storage> storage;

	struct SWPS3_thread_data thread_data_array[numthreads];

	for (int i=0;i<numthreads; i++){
		vector <DBSeq> st_seqs;
		if((i+1) < numthreads){
			for(unsigned int j=(i*(seqs.size()/numthreads));j<((seqs.size()/numthreads))*(i+1);j++){
			//for(int j=(i*(seqs.size()/numthreads));j<100;j++){
				st_seqs.push_back(seqs[j]);
			}
		 }else{//last one
			for(unsigned int j=(i*(seqs.size()/numthreads));j<seqs.size();j++){
			//for(int j=(i*(seqs.size()/numthreads));j<100;j++){
				st_seqs.push_back(seqs[j]);
			}
		 }
		 cout << "splitting: " << st_seqs.size() << endl;
		//Same_seq_pthread_storage temp (st_seqs,coverage,identity);
		//storage.push_back(temp);
		thread_data_array[i].thread_id = i;
		thread_data_array[i].seqs = st_seqs;
		thread_data_array[i].identity = identity;
		thread_data_array[i].reports = 100;
		thread_data_array[i].known_seqs = known_seqs;
		vector<DBSeq> keep_seqs1;
		vector<bool> keep_rc1;
		thread_data_array[i].keep_seqs = keep_seqs1;
		thread_data_array[i].keep_rc = keep_rc1;
	}
	pthread_t threads[numthreads];
	void *status;
	int rc;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for(int i=0; i <numthreads; i++){
		cout << "thread: " << i <<endl;
		rc = pthread_create(&threads[i], &attr, SWPS3_Same_seq_pthread_go, (void *) &thread_data_array[i]);
		if (rc){
			printf("ERROR; return code from pthread_create() is %d\n", rc);
			exit(-1);
		}
	}
	pthread_attr_destroy(&attr);
	for(int i=0;i<numthreads; i++){
		cout << "joining: " << i << endl;
		pthread_join( threads[i], &status);
		if (rc){
			printf("ERROR; return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
		printf("Completed join with thread %d status= %ld\n",i, (long)status);
	}
	/*
	 * bring em back and combine for keep_seqs and keep_rc
	 */
	for (int i=0;i<numthreads; i++){
		for(int j=0;j<thread_data_array[i].keep_seqs.size();j++){
			keep_seqs->push_back(thread_data_array[i].keep_seqs[j]);
			keep_rc->push_back(thread_data_array[i].keep_rc[j]);
		}

	}
}

void SQLiteConstructor::remove_duplicates(vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	vector<string> ids;
	vector<string> unique_ids;
	int mycount;

	for(unsigned int i =0; i<keep_seqs->size(); i++){
		ids.push_back(keep_seqs->at(i).get_ncbi_taxid());
		mycount = 0;
		if(unique_ids.size() > 0){
			mycount = (int) count (unique_ids.begin(),unique_ids.end(), keep_seqs->at(i).get_ncbi_taxid());
		}
		if(mycount == 0){
			unique_ids.push_back(keep_seqs->at(i).get_ncbi_taxid());
		}
	}
	vector<int> remove;
	for(unsigned int i=0;i<unique_ids.size();i++){
		mycount = 0;
		mycount = (int) count (ids.begin(),ids.end(), unique_ids[i]);
		if(mycount > 1){
			vector<int> tremove;
			for (int j=0;j<ids.size();j++){
				if(ids[j] == unique_ids[i]){
					remove.push_back(j);
					tremove.push_back(j);
				}
			}
			int bestid = 0;
			int bestcov = 0;
			int bestiden = 0;
			for (int j=0;j<tremove.size();j++){
				DBSeq tseq = keep_seqs->at(tremove[j]);

				int maxiden = 0;
				int maxcov = 0;
				bool rc = false;
				for (int k=0;k<known_seqs->size();k++){
					bool trc = false;
					//vector<double> ret = get_blast_score_and_rc(*known_seqs->at(k), tseq,&trc); //should be pointer?
					//TODO : there was a pointer problem here
					vector<double> ret = get_blast_score_and_rc_cstyle(known_seqs->at(k), tseq, &trc, 0);
					if (ret.size() > 1){
						/*if (ret[0] >maxiden){
							maxiden = ret[0];
						}
						if (ret[1] > maxcov){ // should these be in the same conditional statement
							maxcov = ret[1];
							rc = trc;//need to get it somewhere else -- pointer probably
						}*/
						if (ret[0] >maxiden && ret[1] > maxcov){ // should these be in the same conditional statement
							maxiden = ret[0];
							maxcov = ret[1];
							rc = trc;//need to get it somewhere else -- pointer probably
						}

					}
				}
				if (maxiden >= bestiden && maxcov >= bestcov){
					bestid = tremove[j];
					bestcov = maxcov;
					bestiden = maxiden;
				}

			}
			vector<int>::iterator it;
			it = find(remove.begin(), remove.end(),bestid);
			//++it;
			remove.erase(it);
		}
	}
	//testin
	sort(remove.begin(),remove.end());
	reverse(remove.begin(),remove.end());
	for(unsigned int i=0;i<remove.size();i++){
		keep_seqs->erase(keep_seqs->begin()+remove[i]);
	}
	for(unsigned int i=0;i<remove.size();i++){
		keep_rc->erase(keep_rc->begin()+remove[i]);
	}
	//uses a bit too much memory
	/*
	vector<DBSeq> seqremoves;
	for(unsigned int i=0;i<remove.size();i++){
		//cout << i << " " << remove[i] << endl;
		seqremoves.push_back(keep_seqs->at(remove[i]));
	}
	for(unsigned int i=0;i<seqremoves.size();i++){
		vector<DBSeq>::iterator it;
		it = find(keep_seqs->begin(), keep_seqs->end(), seqremoves[i]);
		//++it;
		keep_seqs->erase(it);
		//keep_rc->erase(it);
	}
	vector<bool> rckeep;
	for(unsigned int i=0;i<keep_rc->size();i++){
		bool x = false;
		for(unsigned int j=0;j<remove.size();j++){
			if(remove[j]==i)
				x = true;
		}
		if(x == false){
			rckeep.push_back(keep_rc->at(i));
		}
	}
	keep_rc->clear();
	for(unsigned int i=0;i<rckeep.size();i++){
		keep_rc->push_back(rckeep[i]);
	}*/
}

void SQLiteConstructor::remove_duplicates_SWPS3(vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	vector<string> ids;
	vector<string> unique_ids;
	int mycount;

	//uses database taxon ids for dups
	for(unsigned int i =0; i<keep_seqs->size(); i++){
		ids.push_back(keep_seqs->at(i).get_id());
		mycount = 0;
		if(unique_ids.size() > 0){
			mycount = (int) count (unique_ids.begin(),unique_ids.end(), keep_seqs->at(i).get_id());
		}
		if(mycount == 0){
			unique_ids.push_back(keep_seqs->at(i).get_id());
		}
	}

	//uses NCBI taxon ids for dups
	/*
	for(unsigned int i =0; i<keep_seqs->size(); i++){
		ids.push_back(keep_seqs->at(i).get_ncbi_taxid());
		mycount = 0;
		if(unique_ids.size() > 0){
			mycount = (int) count (unique_ids.begin(),unique_ids.end(), keep_seqs->at(i).get_ncbi_taxid());
		}
		if(mycount == 0){
			unique_ids.push_back(keep_seqs->at(i).get_ncbi_taxid());
		}
	}
	*/

	/*
	 * get the best score for each known seq
	 */
	vector<int> scores;
	SBMatrix mat = swps3_readSBMatrix( "EDNAFULL" );
	for(int i=0;i<known_seqs->size();i++){
		//TODO : there was a pointer problem here
		scores.push_back(get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(i),&known_seqs->at(i)));
	}

	vector<int> remove;
	for(unsigned int i=0;i<unique_ids.size();i++){
		mycount = 0;
		mycount = (int) count (ids.begin(),ids.end(), unique_ids[i]);
		if(mycount > 1){
			vector<int> tremove;
			for (int j=0;j<ids.size();j++){
				if(ids[j] == unique_ids[i]){
					remove.push_back(j);
					tremove.push_back(j);
				}
			}
			int bestid = 0;
			double bestiden = 0;
			for (int j=0;j<tremove.size();j++){
				DBSeq tseq = keep_seqs->at(tremove[j]);
				double maxiden = 0;
				bool rc = false;
				for (int j=0;j<known_seqs->size();j++){
					bool trc = false;
					//TODO : there was a pointer problem here
					int ret = get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j), & tseq);
					double tsc = double(ret)/double(scores[j]);
					Sequence tseqrc;
					tseqrc.set_id(tseq.get_id());
					tseqrc.set_sequence(tseq.reverse_complement());
					//TODO : there was a pointer problem here
					int retrc = get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j), &tseqrc);
					if(retrc > ret){
						trc = true;
						tsc = double(retrc)/double(scores[j]);
					}
					if (tsc > maxiden){
						maxiden = tsc;
						rc = trc;
					}
				}
				if (maxiden >= bestiden){
					bestid = tremove[j];
					bestiden = maxiden;
				}

			}
			vector<int>::iterator it;
			it = find(remove.begin(), remove.end(),bestid);
			remove.erase(it);
		}
	}
	sort(remove.begin(),remove.end());
	reverse(remove.begin(),remove.end());
	for(unsigned int i=0;i<remove.size();i++){
		keep_seqs->erase(keep_seqs->begin()+remove[i]);
	}
	for(unsigned int i=0;i<remove.size();i++){
		keep_rc->erase(keep_rc->begin()+remove[i]);
	}
}

void SQLiteConstructor::reduce_genomes(vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	/*
	 * get the best score for each known seq
	 */
	vector<int> scores;
	SBMatrix mat = swps3_readSBMatrix( "EDNAFULL" );
	for(int j=0;j<known_seqs->size();j++){
		//TODO : there was a pointer problem here
		scores.push_back(get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j),&known_seqs->at(j)));
	}
	for(unsigned int i =0; i<keep_seqs->size(); i++){
		if(keep_seqs->at(i).get_sequence().size() > 10000){
			cout << "shrinking a genome: "<< keep_seqs->at(i).get_id() << endl;
			DBSeq tseq = keep_seqs->at(i);
			double maxiden = 0;
			bool rc = false;
			int maxknown = 0;
			for (int j=0;j<known_seqs->size();j++){
				bool trc = false;
				//TODO : there was a pointer problem here
				int ret = get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j), & tseq);
				double tsc = double(ret)/double(scores[j]);
				Sequence tseqrc;
				tseqrc.set_id(tseq.get_id());
				tseqrc.set_sequence(tseq.reverse_complement());
				//TODO : there was a pointer problem here
				int retrc = get_swps3_score_and_rc_cstyle(mat,&known_seqs->at(j), &tseqrc);
				if(retrc > ret){
					trc = true;
					tsc = double(retrc)/double(scores[j]);
				}
				if (tsc > maxiden){
					maxiden = tsc;
					rc = trc;
					maxknown = j;
				}
			}
			/*
			 * shrink with phyutility
			 */
			const string tempfile = "TEMPFILES/genome_shrink";
			vector<Sequence> sc1; 
			FastaUtil seqwriter;
			if(keep_rc->at(i) == false)
				sc1.push_back(keep_seqs->at(i));
			else{
				Sequence tseqrc;
				tseqrc.set_id(keep_seqs->at(i).get_id());
				tseqrc.set_sequence(keep_seqs->at(i).reverse_complement());
				sc1.push_back(tseqrc);
			}
			//for (int j=0;j<known_seqs->size();j++){
			//TODO : there was a pointer problem here
				sc1.push_back(known_seqs->at(maxknown));
			//}
			seqwriter.writeFileFromVector(tempfile,sc1);
			const char * cmd = "mafft --auto TEMPFILES/genome_shrink > TEMPFILES/genome_shrink_aln";
			cout << "aligning" << endl;
			FILE *fp = popen(cmd, "r" );
			char buff[1000];
			while ( fgets( buff, sizeof buff, fp ) != NULL ) {//doesn't exit out
				string line(buff);
			}
			pclose( fp );
			const char * cmd2 = "phyutility -clean 0.5 -in TEMPFILES/genome_shrink_aln -out TEMPFILES/genome_shrink_out";
			cout << "cleaning" << endl;
			FILE *fp2 = popen(cmd2, "r" );
			char buff2[1000];
			while ( fgets( buff2, sizeof buff2, fp2 ) != NULL ) {//doesn't exit out
				string line(buff2);
			}
			pclose( fp2 );
			/*
			 * reading in the sequencing and replacing
			 */
			FastaUtil seqreader;
			vector<Sequence> sequences;
			seqreader.readFile("TEMPFILES/genome_shrink_out", sequences);
			for (int j=0;j<sequences.size();j++){
				//TODO : there was a pointer problem here
				if (sequences.at(j).get_id() ==  keep_seqs->at(i).get_id()){
					keep_seqs->at(i).set_sequence(sequences.at(j).get_sequence());
					keep_rc->at(i) = false;
				}
			}
			cout << "shrunk size: "<< keep_seqs->at(i).get_id() << endl;
		}
	}
}

vector<string> SQLiteConstructor::get_final_children(string inname_id){
	vector<string> ids;
	ids.push_back(inname_id);
	vector<string>	keepids;

	Database conn(db);
	//testing if tip
	bool tip = true;
	//end testing
	while(!ids.empty()){
		string sql = "SELECT ncbi_id FROM taxonomy WHERE parent_ncbi_id = "+ids.back()+" and name_class='scientific name';";
		ids.pop_back();
		Query query(conn);
		query.get_result(sql);
		//StoreQueryResult R = query.store();
		if(query.num_rows() > 0){
			tip = false;
		}
		while(query.fetch_row()){
			string tid = to_string(query.getval());
			ids.push_back(tid);
			keepids.push_back(tid);
		}
		query.free_result();
	}
	if (tip == true){
		keepids.push_back(inname_id);
	}

	vector<string> allids;
	vector<string>allnames;

	for(int i=0;i<keepids.size();i++){
		string sql = "SELECT name,name_class FROM taxonomy WHERE ncbi_id = ";
		sql += keepids[i];
		Query query(conn);
		query.get_result(sql);
		//StoreQueryResult R = query.store();
		while(query.fetch_row()){
			//string tid = R[j][0].c_str();
			string tn = query.getstr();
			string cln = query.getstr();
			if(cln.find("scientific")!=string::npos && tn.find("environmental")==string::npos && cln.find("environmental")==string::npos){
				allids.push_back(keepids[i]); //was taxon id, now ncbi id
				allnames.push_back(tn);
			}
		}
		query.free_result();
	}
	return allids;
}

void SQLiteConstructor::get_seqs_for_names(string inname_id, vector<DBSeq> * seqs, vector<bool> * rcs, vector<DBSeq> * temp_seqs, vector<bool> * temp_rc){
	vector<string> final_ids;
	final_ids = get_final_children(inname_id);
	for(unsigned int i=0;i<seqs->size();i++){
		//string tid = seqs->at(i).get_tax_id();
		string tid = seqs->at(i).get_ncbi_taxid();
		int mycount = 0;
		mycount = (int) count (final_ids.begin(),final_ids.end(), tid);
		if(mycount > 0){
			temp_seqs->push_back(seqs->at(i));
			temp_rc->push_back(rcs->at(i));
		}
	}
}

void SQLiteConstructor::make_mafft_multiple_alignment(vector<DBSeq> * inseqs,vector<bool> * rcs){
	//make file
	vector<double> retvalues;
	FastaUtil seqwriter1;
	vector<Sequence> sc1;
	for(unsigned int i=0;i<inseqs->size();i++){
		if(rcs->at(i) == false){
			sc1.push_back(inseqs->at(i));
		}else{
			Sequence tseqrc;
			tseqrc.set_id(inseqs->at(i).get_id());
			tseqrc.set_sequence(inseqs->at(i).reverse_complement());
			sc1.push_back(tseqrc);
			//inseqs->at(i).perm_reverse_complement();
		}
	}
	const string fn1 = "TEMPFILES/tempfile";
	seqwriter1.writeFileFromVector(fn1,sc1);

	//make alignment
	const char * cmd = "mafft --thread 2 --auto TEMPFILES/tempfile > TEMPFILES/outfile";
	cout << "aligning" << endl;
	FILE *fp = popen(cmd, "r" );
	char buff[1000];
	while ( fgets( buff, sizeof buff, fp ) != NULL ) {//doesn't exit out
		string line(buff);
	}
	pclose( fp );
}
/*
 * should replace paup
 * delete paup when done testing
 */

double SQLiteConstructor::calculate_MAD_quicktree(){
	const char * phcmd = "phyutility -concat -in TEMPFILES/outfile -out TEMPFILES/outfile.nex";
	FILE *phfp = popen(phcmd, "r" );
	pclose( phfp );

	ifstream infile;
	ofstream outfile;
	infile.open ("TEMPFILES/outfile.nex",ios::in);
	outfile.open ("TEMPFILES/outfile.stoc",ios::out);
	bool begin = false;
	bool end = false;
	string line;
	/*
	 * convert to stockholm format
	 */
	while(getline(infile,line)){
		if (line.find("MATRIX") != string::npos){
			begin = true;
		}else if ((begin == true && end == false) && line.find_first_of(";") != string::npos){
			end = true;
		}else if (begin == true && end == false){
			std::string::size_type begin = line.find_first_not_of("\t");
			//std::string::size_type end   = line.find_last_not_of("\t");
			std::string::size_type end = line.size();
			std::string trimmed = line.substr(begin, end-begin + 1);
			outfile << trimmed << endl;
		}
	}
	infile.close();
	outfile.close();

	const char * cmd = "quicktree -in a -out m TEMPFILES/outfile.stoc > TEMPFILES/dist";
	cout << "calculating distance" << endl;
	FILE *fp = popen(cmd, "r" );
	char buff[1000];
	while ( fgets( buff, sizeof buff, fp ) != NULL ) {//doesn't exit out
		string line(buff);
	}
	pclose( fp );

	vector<double> p_values;
	vector<double> jc_values;

	/*
	 * read the matrix
	 */
	//string line;
	ifstream pfile ("TEMPFILES/dist");
	vector<string> tokens;
	int nspecies = 0;
	int curspecies = 0;
	begin = true;
	if (pfile.is_open()){
		while (! pfile.eof() ){
			getline (pfile,line);
			string del("\t ");
			tokens.clear();
			Tokenize(line, tokens, del);
			if(tokens.size() > 1){
				double n1;
				for(int j = curspecies; j < nspecies-1;j++){
						n1 = atof(tokens.at(j+2).c_str());
						p_values.push_back(n1);
						//jc will be (-(3./4.)*math.log(1-(4./3.)*p))
						jc_values.push_back((-(3./4.)*log(1-(4./3.)*n1)));
				}
				curspecies += 1;
			}else if (begin == true){
				begin = false;
				TrimSpaces(line);
				nspecies = atoi(line.c_str());
			}
		}
		pfile.close();
	}
	vector<double>all_abs;
	double med = 0;
	for (unsigned int i=0;i<p_values.size();i++){
		all_abs.push_back(fabs(jc_values[i]-p_values[i]));
	}
	med = median(all_abs);
	cout << "median: " << med << endl;
	vector<double> all_meds;
	for (unsigned int i=0;i<p_values.size();i++){
		all_meds.push_back(fabs(med-all_abs[i]));
	}
	return 1.4826*(median(all_meds));
}

double SQLiteConstructor::calculate_MAD_quicktree_sample(vector<DBSeq> * inseqs,vector<bool> * rcs){
	srand ( time(NULL) );
	vector<int> rands;
	for(int i=0;i<1000;i++){
		int n = rand() % inseqs->size();
		bool x = false;
		for(int j=0;j<rands.size();j++){
			if(n == rands[j]){
				x = true;
			}continue;
		}
		if(x == true){
			i--;
		}else{
			rands.push_back(n);
		}
	}
	sort(rands.begin(),rands.end());
	vector<DBSeq> tseqs;
	vector<bool> trcs;
	for(int i=0;i<1000;i++){
		tseqs.push_back(inseqs->at(rands[i]));
		trcs.push_back(rcs->at(rands[i]));
	}
	make_mafft_multiple_alignment(&tseqs,&trcs);
	return calculate_MAD_quicktree();
}


void SQLiteConstructor::saturation_tests(string name_id, vector<DBSeq> * keep_seqs, vector<bool> * keep_rc){
	vector<string> name_ids;
	vector<string> names;
	name_ids.push_back(name_id);
	names.push_back(clade_name);

	vector<DBSeq> orphan_seqs;
	vector<bool> orphan_seqs_rc;
	vector<Sequence> allseqs; 
	for(int i=0;i<keep_seqs->size();i++){
		if(keep_rc->at(i) == false){
			allseqs.push_back(keep_seqs->at(i));
		}else{
			Sequence tseqrc;
			tseqrc.set_id(keep_seqs->at(i).get_id());
			tseqrc.set_sequence(keep_seqs->at(i).reverse_complement());
			allseqs.push_back(tseqrc);
		}
	}

	write_gi_numbers(keep_seqs);

	string name;
	while(!names.empty()){
		name_id = name_ids.back();
		name_ids.pop_back();
		name = names.back();
		names.pop_back();
		vector<DBSeq> * temp_seqs = new vector<DBSeq>();
		vector<bool> * temp_rcs = new vector<bool>();
		temp_seqs->empty();
		get_seqs_for_names(name_id,keep_seqs,keep_rc,temp_seqs,temp_rcs);
		if(temp_seqs->size() == 1){
			/*
			 * use to make an orphan but rather just make a singleton file
			 */
			cout << name << " " << temp_seqs->size() << endl;
			//make file
			FastaUtil seqwriter1;
			vector<Sequence> sc1;
			for(int i=0;i<temp_seqs->size();i++){
				//need to implement a better way, but this is it for now
				int eraseint=0;
				for(int zz=0;zz<allseqs.size();zz++){
					if (temp_seqs->at(i).get_id() == allseqs[zz].get_id()){
						eraseint = zz;
						break;
					}
				}
				allseqs.erase(allseqs.begin()+eraseint);
				if(temp_rcs->at(i) == false)
					sc1.push_back(temp_seqs->at(i));
				else{
					Sequence tseqrc;
					tseqrc.set_id(temp_seqs->at(i).get_id());
					tseqrc.set_sequence(temp_seqs->at(i).reverse_complement());
					sc1.push_back(tseqrc);
				}
			}
			string fn1 = gene_name;
			fn1 += "/" + name;
			seqwriter1.writeFileFromVector(fn1,sc1);
		}else if (temp_seqs->size() == 0){
			continue;
		}else{
			cout << name << " " << temp_seqs->size() << endl;
			double mad;
			if(temp_seqs->size() > 2){
				//PAUP
				if (temp_seqs->size() < 3000){
					make_mafft_multiple_alignment(temp_seqs,temp_rcs);
					//mad = calculate_MAD_PAUP();
					mad = calculate_MAD_quicktree();
				}else if(temp_seqs->size() < 10000){
					//need to make this happen 10 tens and average
					mad = 0;
					for (int i=0;i<10;i++)
						//mad = mad + (calculate_MAD_PAUP_sample(temp_seqs,temp_rcs)/10.0);
						mad = mad + (calculate_MAD_quicktree_sample(temp_seqs,temp_rcs)/10.0);
					mad = mad * 2; //make sure is conservative
				}else{
					mad = mad_cutoff + 1;//make sure it gets broken up
				}
			}else{
				mad = 0;
			}
			cout << "mad: "<<mad << endl;
			//if mad scores are good, store result
			if (mad <= mad_cutoff){
				FastaUtil seqwriter1;
				vector<Sequence> sc1; 
				for(int i=0;i<temp_seqs->size();i++){
					//need to implement a better way, but this is it for now
					int eraseint=0;
					for(int zz=0;zz<allseqs.size();zz++){
						if (temp_seqs->at(i).get_id() == allseqs[zz].get_id()){
							eraseint = zz;
							break;
						}
					}
					allseqs.erase(allseqs.begin()+eraseint);
					if(temp_rcs->at(i) == false){
						sc1.push_back(temp_seqs->at(i));
					}else{
						Sequence tseqrc;
						tseqrc.set_id(temp_seqs->at(i).get_id());
						tseqrc.set_sequence(temp_seqs->at(i).reverse_complement());
						sc1.push_back(tseqrc);
					}
				}
				string fn1 = gene_name;
				fn1 += "/" + name;
				seqwriter1.writeFileFromVector(fn1,sc1);
			}
			//if mad scores are bad push the children into names
			else{
				vector<string>child_ids;
				Database conn(db);
				string sql = "SELECT ncbi_id FROM taxonomy WHERE parent_ncbi_id = ";
				sql += name_id;
				sql += " and name_class = 'scientific name';";
				Query query(conn);
				query.get_result(sql);
				//StoreQueryResult R = query.store();
				while(query.fetch_row()){
					string sql2 = "SELECT name,name_class FROM taxonomy WHERE ncbi_id = ";
					string resstr = to_string(query.getval());
					sql2 += resstr;
					sql2 += " and name_class = 'scientific name';";
					Query query2(conn);
					query2.get_result(sql2);
					//StoreQueryResult R2 = query2.store();
					while(query2.fetch_row()){
						string tn = query2.getstr();
						string cln = query2.getstr();
						if(cln.find("scientific")!=string::npos && tn.find("environmental")==string::npos && cln.find("environmental")==string::npos){
							string tid = resstr;
							name_ids.push_back(tid);
							names.push_back(tn);
						}
					}
					query2.free_result();
				}
				query.free_result();
			}
		}
//		allseqs->at("173412");
//		173412
//		173413
//		173430
//		173706
		delete (temp_seqs);
		delete (temp_rcs);
	}
	/*
	 * deal with the singletons
	 */
	cout << "leftovers: " << allseqs.size() << endl;
	for(int i=0;i<allseqs.size();i++){
		Database conn(db);
		vector<Sequence> sc1; 
		//TODO : there was a pointer problem here
		sc1.push_back(allseqs.at(i));
		string name;
		string sql = "SELECT name,name_class FROM taxonomy WHERE ncbi_id = ";
		//TODO : there was a pointer problem here
		sql += allseqs.at(i).get_id();
		cout <<"-"<<allseqs.at(i).get_id()<<endl;
		Query query(conn);
		query.get_result(sql);
		//StoreQueryResult R = query.store();
		while(query.fetch_row()){
			string tn = query.getstr();
			string cln = query.getstr();
			if(cln.find("scientific")!=string::npos && tn.find("environmental")==string::npos && cln.find("environmental")==string::npos){
				string tid = allseqs.at(i).get_id();
				name = tn;
			}
		}
		query.free_result();
		cout << name << endl;
		FastaUtil seqwriter1;
		string fn1 = gene_name;
		fn1 += "/" + name;
		seqwriter1.writeFileFromVector(fn1,sc1);
	 }
}

/*
 * this stores the gi numbers for reference
 */

void SQLiteConstructor::write_gi_numbers(vector<DBSeq> * dbs){
	for(int i=0;i<dbs->size();i++){
		//gifile << dbs->at(i).get_tax_id() << "\t"; //don't need this anymore
		gifile << dbs->at(i).get_ncbi_taxid() << "\t";
		//gifile << dbs->at(i).get_accession() << endl;
		gifile << dbs->at(i).get_gi() << endl;
	}
}



/*should no longer be in use
 * double SQLiteConstructor::calculate_MAD_PAUP(){
	const char * phcmd = "phyutility -concat -in TEMPFILES/outfile -out TEMPFILES/outfile.nex";
	FILE *phfp = popen(phcmd, "r" );
	pclose( phfp );

	string text = "\nBEGIN PAUP;\ndset distance=p;\nsavedist file=TEMPFILES/p replace=yes;\ndset distance=jc;\nsavedist file=TEMPFILES/jc replace=yes;\nquit;\nEND;\n";
	ofstream myfile;
	myfile.open ("TEMPFILES/outfile.nex",ios::app);
	myfile << text;
	myfile.close();

	const char * cmd = "paup TEMPFILES/outfile.nex";
	cout << "calculating distance" << endl;
	FILE *fp = popen(cmd, "r" );
	char buff[1000];
	while ( fgets( buff, sizeof buff, fp ) != NULL ) {//doesn't exit out
		string line(buff);
	}
	pclose( fp );

	vector<double> p_values;
	vector<double> jc_values;

	string line;
	ifstream pfile ("TEMPFILES/p");
	vector<string> tokens;
	if (pfile.is_open()){
		while (! pfile.eof() ){
			getline (pfile,line);
			string del("\t");
			tokens.clear();
			Tokenize(line, tokens, del);
			if(tokens.size() > 1){
				double n1;
				n1 = atof(tokens.at(2).c_str());
				p_values.push_back(n1);
			}
		}
		pfile.close();
	}
	ifstream jcfile ("TEMPFILES/jc");
	if (jcfile.is_open()){
		while (! jcfile.eof() ){
			getline (jcfile,line);
			string del("\t");
			tokens.clear();
			Tokenize(line, tokens, del);
			if(tokens.size() > 1){
				double n1;
				n1 = atof(tokens.at(2).c_str());
				jc_values.push_back(n1);
			}
		}
		jcfile.close();
	}
	vector<double>all_abs;
	double med = 0;
	for (unsigned int i=0;i<p_values.size();i++){
		all_abs.push_back(abs(jc_values[i]-p_values[i]));
	}
	med = median(all_abs);
	cout << "median: " << med << endl;
	vector<double> all_meds;
	for (unsigned int i=0;i<p_values.size();i++){
		all_meds.push_back(abs(med-all_abs[i]));
	}
	return 1.4826*(median(all_meds));
}

double SQLiteConstructor::calculate_MAD_PAUP_sample(vector<DBSeq> * inseqs,vector<bool> * rcs){
	srand ( time(NULL) );
	vector<int> rands;
	for(int i=0;i<1000;i++){
		int n = rand() % inseqs->size();
		bool x = false;
		for(int j=0;j<rands.size();j++){
			if(n == rands[j]){
				x = true;
			}continue;
		}
		if(x == true){
			i--;
		}else{
			rands.push_back(n);
		}
	}
	sort(rands.begin(),rands.end());
	vector<DBSeq> tseqs;
	vector<bool> trcs;
	for(int i=0;i<1000;i++){
		tseqs.push_back(inseqs->at(rands[i]));
		trcs.push_back(rcs->at(rands[i]));
	}
	make_mafft_multiple_alignment(&tseqs,&trcs);
	return calculate_MAD_PAUP();
}
 */
