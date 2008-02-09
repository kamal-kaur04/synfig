/* === S Y N F I G ========================================================= */
/*!	\file target_tile.cpp
**	\brief Template File
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "target_tile.h"
#include "string.h"
#include "surface.h"
#include "render.h"
#include "canvas.h"
#include "context.h"
#include "general.h"
#include <ETL/clock>

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;

/* === M A C R O S ========================================================= */
const unsigned int	DEF_TILE_WIDTH = TILE_SIZE / 2;
const unsigned int	DEF_TILE_HEIGHT= TILE_SIZE / 2;

#define SYNFIG_OPTIMIZE_LAYER_TREE 	1
#ifdef _DEBUG
// #define SYNFIG_DISPLAY_EFFICIENCY	1
#endif

/* === G L O B A L S ======================================================= */

/* === P R O C E D U R E S ================================================= */

/* === M E T H O D S ======================================================= */

Target_Tile::Target_Tile():
	threads_(2),
	tile_w_(DEF_TILE_WIDTH),
	tile_h_(DEF_TILE_HEIGHT),
	curr_tile_(0),
	clipping_(true)
{
	curr_frame_=0;
}

int
Target_Tile::next_frame(Time& time)
{
	int
		total_frames(1),
		frame_start(0),
		frame_end(0);
	Time
		time_start(0),
		time_end(0);

	// If the description's end frame is equal to
	// the start frame, then it is assumed that we
	// are rendering only one frame. Correct it.
	if(desc.get_frame_end()==desc.get_frame_start())
		desc.set_frame_end(desc.get_frame_start()+1);

	frame_start=desc.get_frame_start();
	frame_end=desc.get_frame_end();
	time_start=desc.get_time_start();
	time_end=desc.get_time_end();

	// Calculate the number of frames
	total_frames=frame_end-frame_start;
	if(total_frames<=0)total_frames=1;

	//RendDesc rend_desc=desc;
	//rend_desc.set_gamma(1);

//	int total_tiles(total_tiles());
	time=(time_end-time_start)*curr_frame_/total_frames+time_start;
	curr_frame_++;

/*	synfig::info("curr_frame_: %d",curr_frame_);
	synfig::info("total_frames: %d",total_frames);
	synfig::info("time_end: %s",time_end.get_string().c_str());
	synfig::info("time_start: %s",time_start.get_string().c_str());
*/
//	synfig::info("time: %s",time.get_string().c_str());

	return total_frames- curr_frame_+1;
}

int
Target_Tile::next_tile(int& x, int& y)
{
	// Width of the image(in tiles)
	int tw(rend_desc().get_w()/tile_w_);
	int th(rend_desc().get_h()/tile_h_);

	// Add the last tiles (which will be clipped)
	if(rend_desc().get_w()%tile_w_!=0)tw++;
	if(rend_desc().get_h()%tile_h_!=0)th++;

	x=(curr_tile_%tw)*tile_h_;
	y=(curr_tile_/tw)*tile_w_;

	curr_tile_++;
	return (tw*th)-curr_tile_+1;
}

bool
synfig::Target_Tile::render_frame_(Context context,ProgressCallback *cb)
{
	if(tile_w_<=0||tile_h_<=0)
	{
		if(cb)cb->error(_("Bad Tile Size"));
		return false;
	}
	const RendDesc &rend_desc(desc);
#define total_tiles total_tiles()

	etl::clock total_time;
	etl::clock::value_type work_time(0);
	etl::clock::value_type find_tile_time(0);
	etl::clock::value_type add_tile_time(0);
	total_time.reset();

	// If the quality is set to zero, then we
	// use the parametric scanline-renderer.
	if(get_quality()==0)
	{
		Surface surface;

		RendDesc tile_desc;
		int x,y,w,h;
		int i;
		etl::clock tile_timer;
		tile_timer.reset();
		while((i=next_tile(x,y)))
		{
			find_tile_time+=tile_timer();
			SuperCallback	super(cb,(total_tiles-i+1)*1000,(total_tiles-i+2)*1000,total_tiles*1000);
			if(!super.amount_complete(0,1000))
				return false;
			//if(cb && !cb->amount_complete(total_tiles-i,total_tiles))
			//	return false;

			// Perform clipping on the tile
			if(clipping_)
			{
				w=x+tile_w_<rend_desc.get_w()?tile_w_:rend_desc.get_w()-x;
				h=y+tile_h_<rend_desc.get_h()?tile_h_:rend_desc.get_h()-y;
				if(w<=0||h<=0)continue;
			}
			else
			{
				w=tile_w_;
				h=tile_h_;
			}

			tile_desc=rend_desc;
			tile_desc.set_subwindow(x,y,w,h);
			if(!parametric_render(context, surface, tile_desc,&super))
			{
				// For some reason, the parametric renderer failed.
				if(cb)cb->error(_("Parametric Renderer Failure"));
				return false;
			}
			else
			{
				if(!surface)
				{
					if(cb)cb->error(_("Bad surface"));
					return false;
				}
				if(get_remove_alpha())
					for(int i=0;i<surface.get_w()*surface.get_h();i++)
						surface[0][i]=Color::blend(surface[0][i],desc.get_bg_color(),1.0f);

				// Add the tile to the target
				if(!add_tile(surface,x,y))
				{
					if(cb)cb->error(_("add_tile():Unable to put surface on target"));
					return false;
				}
			}
		tile_timer.reset();
		}
	}
	else // If quality is set otherwise, then we use the accelerated renderer
	{
		Surface surface;

		RendDesc tile_desc;
		int x,y,w,h;
		int i;
		etl::clock tile_timer;
		tile_timer.reset();
		while((i=next_tile(x,y)))
		{
			find_tile_time+=tile_timer();
			SuperCallback	super(cb,(total_tiles-i)*1000,(total_tiles-i+1)*1000,total_tiles*1000);
			if(!super.amount_complete(0,1000))
				return false;
//			if(cb && !cb->amount_complete(total_tiles-i,total_tiles))
//				return false;
			// Perform clipping on the tile
			if(clipping_)
			{
				w=x+tile_w_<rend_desc.get_w()?tile_w_:rend_desc.get_w()-x;
				h=y+tile_h_<rend_desc.get_h()?tile_h_:rend_desc.get_h()-y;
				if(w<=0||h<=0)continue;
			}
			else
			{
				w=tile_w_;
				h=tile_h_;
			}

			tile_desc=rend_desc;
			tile_desc.set_subwindow(x,y,w,h);

			etl::clock timer2;
			timer2.reset();

			if(!context.accelerated_render(&surface,get_quality(),tile_desc,&super))
			{
				// For some reason, the accelerated renderer failed.
				if(cb)cb->error(_("Accelerated Renderer Failure"));
				return false;
			}
			else
			{
				work_time+=timer2();
				if(!surface)
				{
					if(cb)cb->error(_("Bad surface"));
					return false;
				}
				if(get_remove_alpha())
					for(int i=0;i<surface.get_w()*surface.get_h();i++)
						surface[0][i]=Color::blend(surface[0][i],desc.get_bg_color(),1.0f);

				etl::clock timer;
				timer.reset();
				// Add the tile to the target
				if(!add_tile(surface,x,y))
				{
					if(cb)cb->error(_("add_tile():Unable to put surface on target"));
					return false;
				}
				add_tile_time+=timer();
			}
			tile_timer.reset();
			signal_progress()();
		}
	}
	if(cb && !cb->amount_complete(total_tiles,total_tiles))
		return false;

#if SYNFIG_DISPLAY_EFFICIENCY==1
	synfig::info(">>>>>> Render Time: %fsec, Find Tile Time: %fsec, Add Tile Time: %fsec, Total Time: %fsec",work_time,find_tile_time,add_tile_time,total_time());
	synfig::info(">>>>>> FRAME EFFICIENCY: %f%%",(100.0f*work_time/total_time()));
#endif
#undef total_tiles
	return true;
}

bool
synfig::Target_Tile::render(ProgressCallback *cb)
{
	SuperCallback super_cb;
	int
		i=0,
		total_frames,
		frame_start,
		frame_end;
	Time
		t=0,
		time_start,
		time_end;

	assert(canvas);
	curr_frame_=0;
	init();
	if( !init() ){
		if(cb) cb->error(_("Target initialization failure"));
		return false;
	}


	// If the description's end frame is equal to
	// the start frame, then it is assumed that we
	// are rendering only one frame. Correct it.
	if(desc.get_frame_end()==desc.get_frame_start())
		desc.set_frame_end(desc.get_frame_start()+1);

	frame_start=desc.get_frame_start();
	frame_end=desc.get_frame_end();
	time_start=desc.get_time_start();
	time_end=desc.get_time_end();

	// Calculate the number of frames
	total_frames=frame_end-frame_start;



	try {
		// Grab the time
		i=next_frame(t);

		//synfig::info("1time_set_to %s",t.get_string().c_str());

		if(i>=1)
		{
		do
		{
			curr_tile_=0;

			// If we have a callback, and it returns
			// false, go ahead and bail. (maybe a use cancel)
			if(cb && !cb->amount_complete(total_frames-(i-1),total_frames))
				return false;

			if(!start_frame(cb))
				return false;

			// Set the time that we wish to render
			//if(!get_avoid_time_sync() || canvas->get_time()!=t)
				canvas->set_time(t);

			Context context;

			#ifdef SYNFIG_OPTIMIZE_LAYER_TREE
			Canvas::Handle op_canvas(Canvas::create());
			op_canvas->set_file_name(canvas->get_file_name());
			optimize_layers(canvas->get_time(), canvas->get_context(), op_canvas);
			context=op_canvas->get_context();
			#else
			context=canvas->get_context();
			#endif

/*
			#ifdef SYNFIG_OPTIMIZE_LAYER_TREE
			Context context;
			Canvas::Handle op_canvas(Canvas::create());
			op_canvas->set_file_name(canvas->get_file_name());
			// Set the time that we wish to render
			canvas->set_time(t);
			optimize_layers(canvas->get_time(), canvas->get_context(), op_canvas);
			context=op_canvas->get_context();
			#else
			Context context;
			// Set the time that we wish to render
			canvas->set_time(t);
			context=canvas->get_context();
			#endif
*/

			if(!render_frame_(context,0))
				return false;
			end_frame();
		}while((i=next_frame(t)));
		//synfig::info("tilerenderer: i=%d, t=%s",i,t.get_string().c_str());
		}
		else
		{
			curr_tile_=0;

			if(!start_frame(cb))
				return false;

			// Set the time that we wish to render
//			if(!get_avoid_time_sync() || canvas->get_time()!=t)
				canvas->set_time(t);

			//synfig::info("2time_set_to %s",t.get_string().c_str());

			Context context;

			#ifdef SYNFIG_OPTIMIZE_LAYER_TREE
			Canvas::Handle op_canvas(Canvas::create());
			op_canvas->set_file_name(canvas->get_file_name());
			optimize_layers(canvas->get_time(), canvas->get_context(), op_canvas);
			context=op_canvas->get_context();
			#else
			context=canvas->get_context();
			#endif

			if(!render_frame_(context, cb))
				return false;
			end_frame();
		}

	}
	catch(String str)
	{
		if(cb)cb->error(_("Caught string :")+str);
		return false;
	}
	catch(std::bad_alloc)
	{
		if(cb)cb->error(_("Ran out of memory (Probably a bug)"));
		return false;
	}
	catch(...)
	{
		if(cb)cb->error(_("Caught unknown error, rethrowing..."));
		throw;
	}
	return true;
}
