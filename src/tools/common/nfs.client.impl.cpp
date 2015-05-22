# pragma once
# include <dsn\internal\nfs.client.impl.h>
# include <queue>
# include <io.h>
# include <direct.h>

namespace dsn { 
	namespace service {

		int nfs_client_impl::_client_request_count = 0;
		std::queue<copy_request*> nfs_client_impl::_req_copy_file_queue;

		void nfs_client_impl::end_copy(
			::dsn::error_code err,
			const copy_response& resp,
			void* context)
		{
			_client_request_count--; // consume
			copy_request* reqc = (copy_request*)context;

			if (err != ::dsn::ERR_SUCCESS)
			{
				reqc->nfs_task->enqueue(err, 0, reqc->nfs_task->node());
				return;
			}

			if (resp.error != ::dsn::ERR_SUCCESS)
			{
				error_code resp_err;
				resp_err.set(resp.error);
				//std::cout << "reply RPC_NFS_COPY err : " << err.to_string() << std::endl;
				reqc->nfs_task->enqueue(resp_err, 0, reqc->nfs_task->node());
				/*
				if (client_request_count < MAXREQUESTCOUNT)
				{
					begin_copy(*reqc, reqc);
					client_request_count++;
				}
				else
				{
					zauto_lock l(_req_copy_file_queue_lock);
					_req_copy_file_queue.push(reqc); // not resend here
				}
				*/
				return;
			}
			else
			{
				std::cout << "reply RPC_NFS_COPY ok" << std::endl;
				if (!_req_copy_file_queue.empty()) // pop a copy file request in queue to execute
				{
					begin_copy(*_req_copy_file_queue.front(), _req_copy_file_queue.front());
					_req_copy_file_queue.pop();
					_client_request_count++;
				}
			}
			//std::cout << "*** call RPC_NFS_NFS_COPY end, return " << resp.file_content.length() << " with content " << resp.file_content.data() << " with err " << err.to_string() << std::endl;
			std::cout << "*** call RPC_NFS_COPY end, return " << "(" << resp.offset << ", " << resp.size << ")" << " with err " << err.to_string() << std::endl;

			std::string file_path = reqc->source_dir + reqc->file_name;
			
			// create file folder if not existed
			for (int i = 0; i < file_path.length(); i++)
			{
				if (file_path[i] == '/')
				{
					if (access(file_path.substr(0, i).c_str(), 6) == -1)
					{
						mkdir(file_path.substr(0, i).c_str());
					}
				}
			}

			handle_t hfile = file::open(file_path.c_str(), O_RDWR | O_CREAT, 0);

			auto task = file::write(
				hfile,
				resp.file_content.data(),
				resp.size,
				resp.offset,
				LPC_NFS_WRITE,
				nullptr,
				std::bind(
				&nfs_client_impl::internal_write_callback,
				this,
				std::placeholders::_1,
				std::placeholders::_2,
				*reqc
				)
				);
			file::close(hfile);
		}

		void nfs_client_impl::end_get_file_size(
			::dsn::error_code err,
			const ::dsn::service::get_file_size_response& resp,
			void* context)
		{
			get_file_size_request* reqc = (get_file_size_request*)context;

			if (err != ::dsn::ERR_SUCCESS)
			{
				reqc->nfs_task->enqueue(err, 0, reqc->nfs_task->node());
				return;
			}

			if (resp.error != ::dsn::ERR_SUCCESS)
			{
				//std::cout << "get file size err : " << err.to_string() << std::endl;
				//begin_get_file_size(*reqc, reqc); // not resend every time 
				error_code resp_err;
				resp_err.set(resp.error);
				reqc->nfs_task->enqueue(resp_err, 0, reqc->nfs_task->node());
				return;
			}
			else
			{
				std::cout << "get file size ok" << std::endl;
			}

			for (int i = 0; i < resp.size_list.size(); i++)
			{
				int32_t size = resp.size_list[i];
				std::cout << "this file size is " << size << ", name is " << resp.file_list[i] << std::endl;

				int32_t req_offset = 0;
				int32_t req_size;
				if (size > MAXBUFSIZE)
					req_size = MAXBUFSIZE;
				else
					req_size = size;

				for (;;)
				{
					copy_request* req = new copy_request;
					req->source = reqc->source;
					req->file_name = resp.file_list[i];
					req->offset = req_offset;
					req->size = req_size;
					req->dst_dir = reqc->dst_dir;
					req->source_dir = reqc->source_dir;
					req->nfs_task = reqc->nfs_task;
					
					if (_client_request_count < MAXREQUESTCOUNT)
					{
						begin_copy(*req, req);
						_client_request_count++;
					}
					else
					{
						zauto_lock l(_req_copy_file_queue_lock);
						_req_copy_file_queue.push(req);
					}

					req_offset += req_size;
					size -= req_size;
					if (size <= 0)
						break;
					if (size > MAXBUFSIZE)
						req_size = MAXBUFSIZE;
					else
						req_size = size;
				}
			}
		}
		
		void nfs_client_impl::begin_remote_copy(std::shared_ptr<remote_copy_request>& rci, aio_task_ptr nfs_task)
		{
			get_file_size_request* req = new get_file_size_request;
			req->source = rci->source;
			req->dst_dir = rci->dest_dir;
			req->file_list = rci->files;
			req->source_dir = rci->source_dir;
			req->overwrite = rci->overwrite;
			req->nfs_task = nfs_task;

			begin_get_file_size(*req, req);
		}
	} 
} 