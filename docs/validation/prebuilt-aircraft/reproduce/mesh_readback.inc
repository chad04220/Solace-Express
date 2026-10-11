  // DIAGNOSTIC ONLY: read actual uploaded vertices and GPU-computed poses. No geometry writes.
  if (const char* dump = getenv("REVIEW_MESH_DUMP")) {
    static std::string lastDump;
    if (lastDump != dump && fp.plane.on) {
      lastDump=dump; updatePartPoses(fp);
      auto it=planeMeshes.find(hullKey(fp,fp.plane.PS[3]>.5f?1:0));
      if(it!=planeMeshes.end()){
        const auto& pm=it->second;
        if(!g_shaderCacheDir.empty()){
          char cacheName[128];snprintf(cacheName,sizeof cacheName,"/mesh_%016llx_%s.bin",(unsigned long long)pm.key,meshStamp(fp.plane.model,fp.plane.PS[3]>.5f?1:0).c_str());
          const std::string cachePath=g_shaderCacheDir+cacheName;
          if(FILE*c=fopen((std::string(dump)+".cache.txt").c_str(),"w")){fprintf(c,"%s\n",cachePath.c_str());fclose(c);}
          printf("MESH CACHE SOURCE %s\n",cachePath.c_str());
        }
        FILE*f=fopen(dump,"w");
        if(f){
          fprintf(f,"part,instance,filter,vertices,min_x,min_y,min_z,max_x,max_y,max_z,world_min_y,gear,model\n");
          FILE* topology=fopen((std::string(dump)+".topology.csv").c_str(),"w");std::unordered_map<int,bool> topologySeen;
          if(topology)fprintf(topology,"part,vertices,indices,triangles,vertex_bytes,index_bytes\n");
          auto readV=[&](GLuint vbo){GLint bytes=0;glBindBuffer(GL_COPY_READ_BUFFER,vbo);glGetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&bytes);std::vector<float>v(size_t(std::max(0,bytes))/4);if(!v.empty())glGetBufferSubData(GL_COPY_READ_BUFFER,0,bytes,v.data());glBindBuffer(GL_COPY_READ_BUFFER,0);return v;};
          auto record=[&](int part,int inst,const std::vector<float>&v,const float*pose,int indices,GLuint ibo){
            if(topology&&!topologySeen[part]){GLint bytes=0;if(ibo){glBindBuffer(GL_COPY_READ_BUFFER,ibo);glGetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&bytes);glBindBuffer(GL_COPY_READ_BUFFER,0);}
              fprintf(topology,"%d,%zu,%d,%d,%zu,%d\n",part,v.size()/8,indices,indices/3,v.size()*sizeof(float),bytes);topologySeen[part]=true;
            }
            for(int filter:{-1,6,8,500}){vec3 lo(1e9f),hi(-1e9f);float worldMin=1e9f;int count=0;
              for(size_t i=0;i+7<v.size();i+=8){vec3 p(v[i],v[i+1],v[i+2]);int material=int(v[i+6]+.5f);
                if(pose)p=vec3(pose[0]*p.x+pose[4]*p.y+pose[8]*p.z+pose[12],pose[1]*p.x+pose[5]*p.y+pose[9]*p.z+pose[13],pose[2]*p.x+pose[6]*p.y+pose[10]*p.z+pose[14]);
                if(filter==500){if(!(part==-1&&fp.plane.model==kAtlas&&fabsf(p.x)>4.5f&&fabsf(p.x)<8.0f&&p.z> -6.5f&&p.z<2.f&&p.y< -1.f))continue;}
                else if(filter>=0&&material!=filter)continue;
                lo=vec3(std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z));hi=vec3(std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z));
                worldMin=std::min(worldMin,fp.plane.pos.y+fp.plane.rot[1]*p.x+fp.plane.rot[4]*p.y+fp.plane.rot[7]*p.z);count++;
              }
              if(count)fprintf(f,"%d,%d,%d,%d,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.5f,%d\n",part,inst,filter,count,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z,worldMin,fp.plane.PS[0],fp.plane.model);
            }
          };
          record(-1,-1,readV(pm.vbo),nullptr,pm.idx,pm.ibo);
          const auto&O=poseOwner[0];int n=int(poseType.size());std::vector<float>poses(size_t(n)*16);GLint prev=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&prev);
          if(n&&fboPartPose){glBindFramebuffer(GL_FRAMEBUFFER,fboPartPose);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,n*4,1,GL_RGBA,GL_FLOAT,poses.data());glBindFramebuffer(GL_FRAMEBUFFER,GLuint(prev));
            for(int inst=O.base;inst<O.base+O.n;++inst)for(const auto&p:pm.parts)if(p.type==poseType[inst])record(p.type,inst,readV(p.vbo),poses.data()+size_t(inst)*16,p.idx,p.ibo);
          }
          fclose(f);if(topology)fclose(topology);printf("MESH READBACK %s\n",dump);
        }
      }
    }
  }
