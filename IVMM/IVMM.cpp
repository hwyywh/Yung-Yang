//O(nk^2mlogm+nk^2)
//nΪ�켣����
//mΪ·����
//kΪĳ���켣��ĺ�ѡ����

#include "stdafx.h"
#include "database.h"
#include "geometry.h"
#include "Graph.h"
using namespace std;

class MAT{
public:
	double **mat;
	int n,m;
	MAT():n(0),m(0),mat(NULL){}
	MAT(int _n,int _m):n(_n),m(_m){
		mat = new double*[n];
		for(int i=0;i<n;++i){
			mat[i] = new double[m];
			memset(mat[i],0,sizeof(double)*m);
		}
	}
	MAT& operator = (const MAT& x){
		double** matOri = mat;
		mat = new double*[x.n];
		for(int i=0;i<x.n;++i){
			mat[i] = new double[x.m];
			for(int j=0;j<x.m;++j)
				mat[i][j] = x.mat[i][j];
		}
		if(matOri != NULL){
			for(int i=0;i<n;++i)
				delete[] matOri[i];
			delete[] matOri;
		}
		n = x.n;
		m = x.m;
		return *this;
	}
	/*~MAT(){
		for(int i=0;i<n;++i)
			delete[] mat[i];
		delete[] mat;
	}*/
};

////////////////////config////////////////////////
string dbname = "osm";//���ݿ�����
string dbport = "5432";//���ݿ�˿ں�
string dbaddr = "127.0.0.1";//���ݿ��ַ
string roadTN = "network";//��·������
int threadNum = 4;//���ڼ�����߳�����
double R = 100;//ѡȡĳ�켣��ĺ�ѡ��ķ�Χ����λΪm
double Sigma = 10;//��̬�ֲ�����λΪm
double miu = 5;
int K = 5;//��ѡ����������
double beta = 7000;//m

//////////////////��������/////////////////////////
//variable
time_t tm;//��ʱ����
double Coef;//norm distribution coef
vector <GeoPoint> P;//�켣��
vector < vector <Point> > candiPoint;//ÿ���켣��ĺ�ѡ�㼯��
vector <MAT> M;
vector < vector <double> > W;
vector < vector <MAT> > fi;
vector <int> vote;
vector <double> fvalue;
//F���ܴ���:F = Fs*Ft
map < pair<int,int> , double > F;


Database *DB;//���ݿ�����ʵ��
Graph *network;

//////////////////End/////////////////////////
//////////not main logic func/////////////////
vector < pair<double,double> > parseString(string str);
bool readConfig();
void loadInitPoint();
void loadCandiPoint();
//////////////////////////////////////////////

bool getTaxiTrajectory(string filePath){
	ifstream fin;
	fin.open(filePath);
	if(!fin) {
		std::cerr<<"File open error!"<<endl;
		fin.close();
		return false;
	}
	else{
		double lat,lon;
		long long carID;
		int y,mon,d,h,min,s;
		char buff[100];
		while(fin.getline(buff,100)){
			sscanf_s(buff,"%lld,%d-%d-%d %d:%d:%d,%lf,%lf",&carID,&y,&mon,&d,&h,&min,&s,&lon,&lat);
			P.push_back(GeoPoint(lat,lon,Date(y,mon,d,h,min,s)));
		}
	}
	fin.close();
	candiPoint.resize(P.size());
	return true;
}

//��ʼ��
//����켣���������ӳ��
bool init(string basePath){
	//network.reset();
	Coef = 1/(sqrt(2*PI)*Sigma);
	P.clear();
	candiPoint.clear();
	F.clear();
	network->reset();
	//splitTrajectory(basePath);
	return getTaxiTrajectory(basePath);
}

double f(double x)
{
	return exp(-SQR(x/beta));
}

//��̬�ֲ�
double N(int i,int t){
	double x = getGeoDis(P[i],candiPoint[i][t]);
	return Coef*exp(-SQR(x-miu)/(2*SQR(Sigma)));
}

//Transmission Probability
//if t == s then return 1; because t must transmit s
double V(double d,Point t,Point s){
	if(t == s) return 1;
	double tmp = network->getCandiShortest(t,s);
	return d/tmp;
}

mutex lock_it;
mutex lockVote;
int it;
DWORD WINAPI calc_candiPoint(LPVOID ptr){
	int upd = (int)P.size();
	int cur;
	while(true){
		lock_it.lock();
		if(it >= upd) {
			lock_it.unlock();
			return 0;
		}
		cur = it;
		++it;
		lock_it.unlock();

		vector <Point> tmp = network->getCandidate(Point(P[cur].longitude,P[cur].latitude),R,K);
		
		candiPoint[cur] = tmp;
	}
}

//����켣��ÿ����ĺ�ѡ�㼯��
//����F,Fs,Ft
vector <Point> FindMatchedSequence(int i,int k,vector <MAT> fi_i){
	tm = clock();
	//std::cerr<<"start FindMatchedSequence "<<i<<" "<<k<<endl;
	vector <Point> res;

	//if(i > 0){
	//	for(int s = 0;s<candiPoint[i].size();++s){
	//		for(int t=0;t<candiPoint[i-1].size();++t){
	//			fprintf(stderr,"%lf ",fi[i][1].mat[t][s]);
	//		}
	//		fprintf(stderr,"\n");
	//	}
	//}

	int totCandiNum = network->totCandiPoint;
	double *fmx = new double[totCandiNum];
	int *pre = new int[totCandiNum];

	for(int t=0;t<candiPoint[0].size();++t)
		fmx[candiPoint[0][t].id] = W[i][0]*N(0,t);

	if(i == 0){//i = 0,set init value be -max because there is no fi[i][0],only have fi[i][1]
		for(int t=0;t<candiPoint[0].size();++t){
			if(t == k) continue;
			fmx[candiPoint[0][t].id] = -1e300;
		}
	}
	else{
		for(int s = 0;s<candiPoint[i].size();++s){
			if(s == k) continue;
			int tSz = 0;
			if(i == 0) tSz = (int)candiPoint[i].size();
			else tSz = (int)candiPoint[i-1].size();
			for(int t=0;t<tSz;++t){
				fi_i[i].mat[t][s] = -1e300;
			}
		}
	}

	int pNum = (int)P.size();
	for(int j=1;j<pNum;++j){
		/*cout<<j<<" ";
		cout<<fi[i][j-1].n<<" "<<fi[i][j-1].m<<" ";
		cout<<candiPoint[j].size()<<" "<<candiPoint[j-1].size()<<endl;*/
		for(int s = 0;s<candiPoint[j].size();++s){
			for(int t=0;t<candiPoint[j-1].size();++t){

				//assert(candiPoint[j][s].id < totCandiNum);
				//assert(candiPoint[j-1][t].id < totCandiNum);
				//assert(t < fi[i][j].n);
				//assert(s < fi[i][j].m);
				double fs = fmx[candiPoint[j][s].id];
				double ft = fmx[candiPoint[j-1][t].id];
				/*assert(t < fi[i][j].n);
				assert(s < fi[i][j].m);
				assert(i < fi.size());
				assert(j < fi[i].size());
				cout<<"n = "<<fi[i][j].n<<" m = "<<fi[i][j].m<<endl;
				cout<<"i = "<<i<<" j = "<<j<<endl;*/
				double fijts = fi_i[j].mat[t][s];
				if(fs < ft+fijts){
					fmx[candiPoint[j][s].id] = ft+fijts;
					pre[candiPoint[j][s].id] = candiPoint[j-1][t].id;
				}
			}
		}
	}

	double mx = fmx[candiPoint[pNum-1][0].id];
	int c = candiPoint[pNum-1][0].id;
	for(int s=0;s<candiPoint[pNum-1].size();++s){
		if(mx < fmx[candiPoint[pNum-1][s].id]){
			mx = fmx[candiPoint[pNum-1][s].id];
			c = candiPoint[pNum-1][s].id;
		}
	}
	fvalue[candiPoint[i][k].id] = mx;
	for(int s = pNum-1;s>0;--s){
		res.push_back(network->getCandiPointById(c));
		c = pre[c];
	}
	res.push_back(network->getCandiPointById(c));
	delete[] pre;
	delete[] fmx;

	//reverse(res.begin(),res.end());
	//std::cerr<<"FindMatchedSequence cost = "<<clock()-tm<<"ms"<<endl;
	return res;
}

DWORD WINAPI interactiveVoting(LPVOID ptr){
	int upbound = (int)P.size();
	int cur = 0;
	/*ofstream fout("seq.txt",ios::app);*/
	while(true)
	{
		lock_it.lock();
		if(it >= upbound) {
			lock_it.unlock();
			return 0;
		}
		cur = it;
		++it;
		lock_it.unlock();

		/*fprintf(stderr,"W[%d] = ",cur);*/
		W[cur].resize(upbound);
		for(int j=0;j<upbound;++j){
			W[cur][j] = f(getGeoDis(P[cur],P[j]));
			/*fprintf(stderr,"%lf ",W[cur][j]);*/
		}
		/*fprintf(stderr,"\n");*/

		/*fprintf(stderr,"fi[%d] = ",cur);*/
		fi[cur].resize(upbound);
		for(int j=1;j<upbound;++j){//j indicate M^j , not exist M^1
			MAT tMat(M[j].n,M[j].m);
			/*fprintf(stderr,"j = %d\n",j);*/
			//assert(j < M.size());
			if(j-1 < cur){
				for(int t = 0;t<tMat.n;++t){
					for(int s=0;s<tMat.m;++s){
						tMat.mat[t][s] = W[cur][j-1]*M[j].mat[t][s];
						/*fprintf(stderr,"%lf ",tMat.mat[t][s]);*/
					}
					/*fprintf(stderr,"\n");*/
				}
			}
			else{
				for(int t = 0;t<tMat.n;++t){
					for(int s=0;s<tMat.m;++s){
						tMat.mat[t][s] = W[cur][j]*M[j].mat[t][s];
						/*fprintf(stderr,"%lf ",tMat.mat[t][s]);*/
					}
					/*fprintf(stderr,"\n");*/
				}
			}
			fi[cur][j] = tMat;
		}
		
		for(int j=0;j<candiPoint[cur].size();++j){
			vector <MAT> tFi;
			tFi.resize(fi[cur].size());
			for(int i=0,num=(int)fi[cur].size();i<num;++i)
				tFi[i] = fi[cur][i];
			vector <Point> Seq = FindMatchedSequence(cur,j,tFi);
			lockVote.lock();
			/*cerr<<"candi id = "<<candiPoint[cur][j].id<<endl;*/
			/*fout<<cur<<" "<<candiPoint[cur][j].id<<":"<<endl;*/
			for(int k=0;k<Seq.size();++k)
			{
				/*cerr<<Seq[k].id<<" ";*/
				/*fout<<Seq[k].id<<" ";*/
				++ vote[Seq[k].id];
			}
			/*fout<<endl;*/
			/*cerr<<endl;*/
			lockVote.unlock();
		}
		
	}
	/*fout.close();*/
}

vector <Point> IVMM(){
	//O(nm) for every point in Trajectory find the candipoint set
	std::cerr<<"start getCandiPoint"<<endl;
	tm = clock();

	//////////���м���candipoint����ʼ��it
	it= 0;
	HANDLE *handle = new HANDLE[threadNum];
	for(int i=0;i<threadNum;++i){
		handle[i] = CreateThread(NULL,0,calc_candiPoint,NULL,0,NULL);
	}
	for(int i=0;i<threadNum;++i){
		WaitForSingleObject(handle[i],INFINITE);
	}
	std::cerr<<"getCandidate cost = "<<clock()-tm<<"ms"<<endl;
	loadCandiPoint();


	int pNum = (int)P.size();

	//����M����,������Ft
	//M[i][j]��ʾֻ����������ѡ��i,jʱ,��j����iת�ƹ�������Ϊ��ȷ��ĸ���
	M.resize(pNum);
	//���⴦����һ������Ϊ��λ���ʾ���Լ����Լ�Ϊ��ȷ��ĸ���Ϊ1
	MAT mat((int)candiPoint[0].size(),(int)candiPoint[0].size());
	for(int i=0;i<mat.n;++i)
		for(int j=0;j<mat.m;++j)
			mat.mat[i][j] = i==j?1:0;
	M[0] = mat;

	std::cerr<<"start calc Matrix M"<<endl;
	for(int i=1;i<pNum;++i){
		int nPre = (int)candiPoint[i-1].size();
		int nCur = (int)candiPoint[i].size();
		MAT tMat(nPre,nCur);
		/*fprintf(stderr,"M[%d] = \n",i);*/
		for(int t = 0;t<nPre;++t){
			for(int s=0;s<nCur;++s){
				tMat.mat[t][s] = N(i,s)
					*V(getGeoDis(P[i-1],P[i]),candiPoint[i-1][t],candiPoint[i][s]);
				/*fprintf(stderr,"%lf ",tMat.mat[t][s]);*/
			}
			/*fprintf(stderr,"\n");*/
		}
		M[i] = tMat;
	}
	std::cerr << "calc Matrix M cost = "<<clock() - tm<<"ms"<<endl;

	//����W����
	//W[i][j]��ʾp[j]��p[i]��Ӱ��
	W.resize(pNum);
	//calc fi
	fi.resize(pNum);
	//calc seq
	int totCandiNum = network->totCandiPoint;
	fvalue.resize(totCandiNum);
	vote.resize(totCandiNum);

	/////////////���м��㣬��ʼ��it
	std::cerr<<"start interactive voting"<<endl;
	tm = clock();
	it = 0;
	for(int i=0;i<threadNum;++i){
		handle[i] = CreateThread(NULL,0,interactiveVoting,NULL,0,NULL);
	}
	for(int i=0;i<threadNum;++i){
		WaitForSingleObject(handle[i],INFINITE);
	}
	delete[] handle;
	std::cerr<<"interactive voting cost = "<<clock()-tm<<"ms"<<endl;

	vector <Point> res;
	for(int i=0;i<pNum;++i){
		int mx = 0;
		double mxFval = 0;
		int pos = 0;
		for(int j=0;j<candiPoint[i].size();++j){
			if(mx < vote[candiPoint[i][j].id] 
			|| (mx == vote[candiPoint[i][j].id] && mxFval < fvalue[candiPoint[i][j].id])
				){
				mx = vote[candiPoint[i][j].id];
				pos = j;
				mxFval = fvalue[candiPoint[i][j].id];
			}
		}
		res.push_back(candiPoint[i][pos]);
	}
	/*ofstream fout("vote.txt");
	for(int i=0;i<vote.size();++i)
		fout<<vote[i]<<endl;
	fout.close();*/
	return res;
}

//��ƥ��ѡ�еĵ㣬·��д�����ݿ�
void writeToDB(vector <Point> Traj){
	//д��ѡ�еĺ�ѡ��
	string SQL = "select * from pg_class where relname = 'trajectory_point'";
	PGresult* res = DB->execQuery(SQL);
	int num = PQntuples(res);
	PQclear(res);

	if(num != 0){
		SQL = "Delete from trajectory_point";
		DB->execUpdate(SQL);
	}
	else{
		SQL = "create table trajectory_point (id integer primary key,way geometry(Point,4326))";
		DB->execUpdate(SQL);
	}
	size_t sz = Traj.size();
	char buffer[500];

	for(int i=0;i<sz;++i){
		sprintf_s(buffer,"insert into trajectory_point values(%d,ST_GeomFromText('Point(%lf %lf)',4326))",i+1,Traj[i].x,Traj[i].y);
		SQL = buffer;
		DB->execUpdate(SQL);
	}

	//д��ƥ��Ĺ켣
	SQL = "select * from pg_class where relname = 'trajectory_line'";
	res = DB->execQuery(SQL);
	num = PQntuples(res);
	PQclear(res);

	if(num != 0){
		SQL = "Delete from trajectory_line";
		DB->execUpdate(SQL);
	}
	else{
		SQL = "create table trajectory_line (id integer primary key,src integer,des integer,way geometry(LineString,4326))";
		DB->execUpdate(SQL);
	}
	sz = Traj.size();
	buffer[500];

	int ID = 0;
	for(int i=1;i<sz;++i){
		if(network->isInSameSeg(Traj[i-1].id,Traj[i].id)){
			//in same seg
			sprintf_s(buffer,"insert into trajectory_line values(%d,%d,%d,ST_GeomFromText('LineString(%lf %lf,%lf %lf)',4326))",ID++,Traj[i-1].id,Traj[i].id,Traj[i-1].x,Traj[i-1].y,Traj[i].x,Traj[i].y);
			SQL = buffer;
			DB->execUpdate(SQL);
		}
		else{
			vector <int> path = network->getPath(Traj[i-1],Traj[i]);

			path.pop_back();//path�а���S,T������λ��һͷһβ
			if(path.empty() || path.size() < 2) {
				
				/*sprintf_s(buffer,"insert into trajectory_line values(%d,%d,%d,ST_GeomFromText('LineString(%lf %lf,%lf %lf)',4326))",ID++,Traj[i-1].id,Traj[i].id,Traj[i-1].x,Traj[i-1].y,Traj[i].x,Traj[i].y);
				SQL = buffer;
				DB->execUpdate(SQL);*/
			}
			else{
				Point cur(network->getPointById(path[1]));
				sprintf_s(buffer,"insert into trajectory_line values(%d,%d,%d,ST_GeomFromText('LineString(%lf %lf,%lf %lf)',4326))",ID++,Traj[i-1].id,Traj[i].id,cur.x,cur.y,Traj[i].x,Traj[i].y);
				SQL = buffer;
				DB->execUpdate(SQL);
				size_t psz = path.size();
				for(int j=2;j<psz;++j){
					cur = network->getPointById(path[j]);
					Point pre(network->getPointById(path[j-1]));
					sprintf_s(buffer,"insert into trajectory_line values(%d,%d,%d,ST_GeomFromText('LineString(%lf %lf,%lf %lf)',4326))",ID++,Traj[i-1].id,Traj[i].id,cur.x,cur.y,pre.x,pre.y);
					SQL = buffer;
					DB->execUpdate(SQL);
				}
				cur = network->getPointById(path[psz-1]);
				sprintf_s(buffer,"insert into trajectory_line values(%d,%d,%d,ST_GeomFromText('LineString(%lf %lf,%lf %lf)',4326))",ID++,Traj[i-1].id,Traj[i].id,Traj[i-1].x,Traj[i-1].y,cur.x,cur.y);
				SQL = buffer;
				DB->execUpdate(SQL);
			}

		}
	}
}

int _tmain(int argc, _TCHAR* argv[]){
	//preProcData();
	//cerr<<"over"<<endl;
	/*if(!readConfig()){
		cerr << "read config.ini error!" <<endl;
		return 0;
	}*/

	DB = new Database(dbname,dbport,dbaddr);
	network = new Graph(roadTN);

	////system("pause");

	string basePath;
	cerr<<"please input file path:";
	//while(cin>>basePath)
	basePath = "input.txt";
	{
		cerr<<"start init..."<<endl;
		tm = clock();
		if(init(basePath) == false){
			cerr << "can not open file "<< basePath<<endl;
			cerr<<"please input file path:";
			//continue;
		}
		cerr<<"init cost "<<clock()-tm<<"ms"<<endl;

		cerr<<"start loadInitPoint..."<<endl;
		tm = clock();
		loadInitPoint();
		cerr<<"loadInitPoint cost "<<clock()-tm<<"ms"<<endl;

		vector <Point> res = IVMM();

		cerr<<"start writeToDB->.."<<endl;
		tm = clock();
		writeToDB(res);
		cerr<<"writeToDB cost "<<clock()-tm<<"ms"<<endl;
		cerr<<"please input file path:";
	}
	
	//Point p1(116.367476925,39.9172884517);
	//Point p2(116.36760548,39.9173675835);
	//Point p(116.367718912,39.9179293381);
	//cout<<getGeoDis(p1,p2)<<endl;
	//cout<<dispToseg(p,p1,p2)<<endl;
	return 0;
}

bool readConfig(){
	ifstream fin;
	fin.open("config.ini");
	if(!fin.is_open()){
		cerr <<"can't open config.ini"<<endl;
		return false;
	}
	int cnt = 0;
	string line,prop;
	while(fin >> line){
		if(line == "" || line[0] == '#')
			continue;
		prop = line;
		fin>>line;
		fin>>line;
		cnt++;
		if(prop == "dbname")
			dbname = line;
		else if(prop == "dbport"){
			dbport = line;
		}
		else if(prop == "dbaddr"){
			dbaddr = line;
		}
		else if(prop == "roadTN"){
			roadTN = line;
		}
		else if(prop == "threadnum"){
			int tmp;
			sscanf_s(line.c_str(),"%d",&tmp);
			threadNum = tmp;
		}
		else if(prop == "K"){
			int tmp;
			sscanf_s(line.c_str(),"%d",&tmp);
			K = tmp;
		}
		else if(prop == "R"){
			double tmp;
			sscanf_s(line.c_str(),"%lf",&tmp);
			R = tmp;
		}
		else if(prop == "sigma"){
			double tmp;
			sscanf_s(line.c_str(),"%lf",&tmp);
			Sigma = tmp;
		}
	}
	fin.close();
	if(cnt == 8) 
		return true;
	else{
		cerr<<"config.ini corrupted��"<<endl;
		return false;
	}
}

//�ѹ켣��������ݿ�
void loadInitPoint(){
	string SQL = "select * from pg_class where relname = 'init_point'";
	PGresult* res = DB->execQuery(SQL);
	int num = PQntuples(res);
	PQclear(res);

	if(num != 0){
		SQL = "Delete from init_point";
		DB->execUpdate(SQL);
	}
	else{
		SQL = "create table init_point (id integer primary key,year integer,month integer,day integer,hour integer,minute integer,second integer,way geometry(Point,4326))";
		DB->execUpdate(SQL);
	}
	size_t sz = P.size();
	char buffer[500];

	for(int i=0;i<sz;++i){
		sprintf_s(buffer,"insert into init_point values(%d,%d,%d,%d,%d,%d,%d,ST_GeomFromText('Point(%lf %lf)',4326))",i+1,P[i].date.year,P[i].date.month,P[i].date.day,P[i].date.hour,P[i].date.minute,P[i].date.second,P[i].longitude,P[i].latitude);
		SQL = buffer;
		DB->execUpdate(SQL);
	}
}

//�Ѻ�ѡ��������ݿ�
void loadCandiPoint(){
	string SQL = "select * from pg_class where relname = 'candi_point'";
	PGresult* res = DB->execQuery(SQL);
	int num = PQntuples(res);
	PQclear(res);

	if(num != 0){
		SQL = "Delete from candi_point";
		DB->execUpdate(SQL);
	}
	else{
		SQL = "create table candi_point (id integer primary key,belong integer,way geometry(Point,4326))";
		DB->execUpdate(SQL);
	}
	size_t sz = candiPoint.size();
	char buffer[500];

	for(int i=0;i<sz;i++){
		size_t candisz = candiPoint[i].size();
		for(int j=0;j<candisz;++j){
			sprintf_s(buffer,"insert into candi_point values(%d,%d,ST_GeomFromText('Point(%lf %lf)',4326))",candiPoint[i][j].id,i+1,candiPoint[i][j].x,candiPoint[i][j].y);
			SQL = buffer;
			DB->execUpdate(SQL);
		}
	}
}

//�����ݿ��ж�����LINESTRING�ַ���ת��Ϊ������
//���ص��vector
vector < pair<double,double> > parseString(string str){
	vector < pair<double,double> > res;
	size_t len = str.size();
	int pre = 11;
	double x,y;
	for(int i=0;i<len;++i){
		if(str[i] == ',' || str[i] == ')'){
			string tmp = str.substr(pre,i-pre);
			//cerr<<tmp<<endl;
			sscanf_s(tmp.c_str(),"%lf %lf",&x,&y);
			res.push_back(make_pair(x,y));
			pre = i+1;
		}
	}
	return res;
}

//����ԭʼ���ݣ���LineString�к��ж���������Ĳ��ֲ�ֺ�������ݿ�
//ֻ��Ҫ����һ��
int Insert(string SQL,int id){
	PGresult* res = DB->execQuery(SQL);

	int tupleNum = PQntuples(res);
	int fieldNum = PQnfields(res);

	for(int i=0;i<tupleNum;i++){
		string content = PQgetvalue(res,i,fieldNum-1);
		vector < pair<double,double> > pts = parseString(content);
		size_t sz = pts.size();
		for(int j=1;j<sz;j++){

			SQL = "insert into network values(";
			SQL += std::to_string(id++)+",0,0,";//id,source,target
			SQL += PQgetvalue(res,i,0);//osm_id
			SQL += ",";
			for(int k=1;k<fieldNum;k++){

				char * ss = PQgetvalue(res,i,k);
				if(strlen(ss) == 0) 
					SQL+="NULL";
				else{
					char * name = PQfname(res,k);
					if(strcmp(name,"name") == 0 || strcmp(name,"tags") == 0){
						SQL += "$$";
						SQL += ss;
						SQL += "$$";
					}
					else if(strcmp(name,"way") == 0){
						SQL += "ST_geomFromText('LINESTRING(";
						SQL += std::to_string(pts[j-1].first);
						SQL += " ";
						SQL += std::to_string(pts[j-1].second);
						SQL += ",";
						SQL += std::to_string(pts[j].first);
						SQL += " ";
						SQL += std::to_string(pts[j].second);
						SQL += ")',900913)";
					}
					else{
						SQL += "'";
						SQL += ss;
						SQL += "'";
					}
				}
				if(k+1 == fieldNum) break;
				SQL += ",";
			}
			SQL += ")";
			//cerr<<SQL<<endl;
			DB->execUpdate(SQL);
		}
	}

	PQclear(res);
	return id;
}

void preProcData(){

	string SQL = "select * from pg_class where relname = 'network'";
	PGresult* res = DB->execQuery(SQL);
	int num = PQntuples(res);
	PQclear(res);

	if(num != 0){
		SQL = "Delete from network";
		DB->execUpdate(SQL);
	}
	else{
		SQL = "";
		ifstream fin;
		fin.open("createNetwork.txt");
		string tmp;
		while(fin>>tmp) SQL += tmp;
		fin.close();
		DB->execUpdate(SQL);
	}

	string Field = "";
	SQL = "select * from allroads";
	res = DB->execQuery(SQL);

	int tupleNum = PQntuples(res);
	int fieldNum = PQnfields(res);

	for(int i=0;i<fieldNum-1;i++){
		Field += "allroads.";
		char * name = PQfname(res,i);
		if(strstr(name,":") == NULL) 
			Field += name;
		else{
			Field += "\"";
			Field += name;
			Field += "\"";
		}
		Field += ",";
	}
	Field += "ST_AsText(way) as way";
	PQclear(res);
	int id = 1;
	id = Insert("select "+ Field + " from allroads",id);
}